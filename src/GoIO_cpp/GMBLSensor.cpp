/*********************************************************************************

Copyright (c) 2010, Vernier Software & Technology
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of Vernier Software & Technology nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL VERNIER SOFTWARE & TECHNOLOGY BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

**********************************************************************************/
// GMBLSensor.cpp

#include "stdafx.h"
#include "GMBLSensor.h"
#include "GCalibrateDataFuncs.h"

//#include "GMBLDataCalibration.h"
//#include "GMBLDataCalibMotion.h"
//#include "GMBLDataCalibRotary.h"
//#include "GApplicationBrain.h"
//#include "GMBLSource.h"
//#include "GMBLBrain.h"
//#include "GLabProCommon.h" 
//#include "GMBLLabProSource.h"
//#include "GMessenger.h"
//#include "GMessages.h"
//#include "GMBLSensorMap.h"
//#include "GProgressMonitor.h"
//#include "GColumn.h"
//#include "GDocument.h"
//#include "GDialogs.h"
#include "GUtils.h"
#include "GTextUtils.h"

#ifdef _DEBUG
#include "GPlatformDebug.h" // for DEBUG_NEW definition
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#ifdef LIB_NAMESPACE
namespace LIB_NAMESPACE {
#endif

GMBLSensor::GMBLSensor(void)
{
	memset(&m_sensorDDSRec, 0, sizeof(m_sensorDDSRec));
	m_sensorDDSRec.MinSamplePeriod = (float) 0.001;
	m_sensorDDSRec.TypSamplePeriod = (float) 0.001;
	m_sensorDDSRec.YminValue = 0.0;
	m_sensorDDSRec.YmaxValue = (float) 1000.0;
	m_sensorDDSRec.CalibrationEquation = kEquationType_Linear;
	for (int i = 0; i < 3; i++)
	{
		m_sensorDDSRec.CalibrationPage[i].CalibrationCoefficientA = 0.0;
		m_sensorDDSRec.CalibrationPage[i].CalibrationCoefficientB = (float) 1.0;
		m_sensorDDSRec.CalibrationPage[i].CalibrationCoefficientC = 0.0;
		strcpy(m_sensorDDSRec.CalibrationPage[i].Units, "Volts");
	}

	//OperationType is a LabPro specific field which implies probeType. Ouch!
	m_sensorDDSRec.OperationType = 14;

	m_sensorDDSRec.Checksum = CalculateDDSDataChecksum(m_sensorDDSRec);
}

GMBLSensor::~GMBLSensor()
{
}
	
unsigned char GMBLSensor::CalculateDDSDataChecksum(const GSensorDDSRec &DDSRec)
{ // RETURN DDS checksum -- XOR of bytes 0-126 (the last byte is the checksum itself)
	unsigned char *pByte = (unsigned char *) &DDSRec;
	unsigned char chCheck = 0;
	for (size_t ix = 0; ix < sizeof(DDSRec) - 1; ix++)
		chCheck ^= pByte[ix];

	return chCheck;
}

bool GMBLSensor::VerifyDDSChecksum(const GSensorDDSRec &DDSRec, // [in] The DDS block 
								   bool bStrictChecksumRequired) // true ==> we will only accept an exact checksum match
{
	// verify checksum before continuing
	unsigned char *pDDSBytes = (unsigned char *) &DDSRec;
	unsigned char chCheck = GMBLSensor::CalculateDDSDataChecksum(DDSRec);
	unsigned char checksum_NOT = ~chCheck;	// LP 2 burned the NOT of the correct checksum; so this is OK
	unsigned char checksum_ROTATE = 0; //Some sensors have the checksum bits rotated!!!

	unsigned char test1 = chCheck;
	for (int jj = 0; jj < 8; jj++)
	{
		checksum_ROTATE = checksum_ROTATE << 1;
		checksum_ROTATE = checksum_ROTATE | (test1 & 1);
		test1 = test1 >> 1;
	}

	// If the DDS memory is all the same value (usually 0xFF), it's no good
	bool bAllSameValue = true;
	for (size_t ii = 1; ii < sizeof(DDSRec); ii++)
		if (pDDSBytes[ii] != pDDSBytes[ii-1])
			bAllSameValue = false;

	bool bCheckOK;
	if (bStrictChecksumRequired)
		bCheckOK = !bAllSameValue && (chCheck == pDDSBytes[127]);
	else
	{ // Let a lot of other cases slide by with an OK
		bCheckOK = !bAllSameValue && ((chCheck == pDDSBytes[127]) || (checksum_NOT == pDDSBytes[127]) || (checksum_ROTATE == pDDSBytes[127]));
		if (!bCheckOK)
		{ //if the checksum fails, check the date code to see that it is in BCD, and that the week value is 52 or less. If so, take that as a pass and proceed. 
			if ((((pDDSBytes[5] >> 4) & 0x0F) <= 9) && ((pDDSBytes[5] & 0x0F) <= 9) &&  	// YearYear 
				(((pDDSBytes[6] >> 4) & 0x0F) <= 5) && ((pDDSBytes[6] & 0x0F) <= 9))		// WeekWeek	-- must be less than 52
				bCheckOK = true;
		}
	}
	
	if (!bCheckOK) 
	{ // DEBUG trace
		cppsstream ssDbg;
		ssDbg << "DDS CHECKSUM FAILED" << endl;
		ssDbg << "my checksum value : 0x" << hex << (short)chCheck << endl;
		ssDbg << "Sensors's checksum value : 0x" << hex << (short)pDDSBytes[127] << endl;
		GSTD_TRACE(ssDbg.str());
	}

	if (bCheckOK)
	{
		//Do some sanity checking.
		if (DDSRec.HighestValidCalPageIndex > 2)
		{
			bCheckOK = false;
			cppsstream ssDbg2;
			ssDbg2 << "DDS ERROR: HighestValidCalPageIndex = " << (short) DDSRec.HighestValidCalPageIndex << endl;
			GSTD_TRACE(ssDbg2.str());
		}
		else
		if (DDSRec.ActiveCalPage > DDSRec.HighestValidCalPageIndex)
		{
			bCheckOK = false;
			cppsstream ssDbg3;
			ssDbg3 << "DDS ERROR: HighestValidCalPageIndex = " << (short) DDSRec.HighestValidCalPageIndex;
			ssDbg3 << " ; ActiveCalPage = " << (short) DDSRec.ActiveCalPage << endl;
			GSTD_TRACE(ssDbg3.str());
		}
	}

	return bCheckOK;
}

void GMBLSensor::UnMarshallDDSRec(
	GSensorDDSRec *pPlatformEndianOutputRec,	//[o] Store the rec here in processor specific endian format.
	const GSensorDDSRec &littleEndianInputRec)	//[i] Input record is in little endian format - data is stored on the sensor in this format.
{
	//Note that (un)marshalling should not affect the checksum because the set of bytes in the structure does not change,
	//just the order of the bytes in the structure. The XOR operation used to calculate the checksum is commutative
	//and associative, so changing the order of the bytes should not effect the outcome.
	*pPlatformEndianOutputRec = littleEndianInputRec;

	unsigned char * pLSB;
	unsigned char * pLMidB;
	unsigned char * pMMidB;
	unsigned char * pMSB;

	pLSB = (unsigned char *) &littleEndianInputRec.MinSamplePeriod;
	pLMidB = pLSB + 1;
	pMMidB = pLMidB + 1;
	pMSB = pMMidB + 1;
	GUtils::OSConvertBytesToFloat(*pLSB, *pLMidB, *pMMidB, *pMSB, &(pPlatformEndianOutputRec->MinSamplePeriod));

	pLSB = (unsigned char *) &littleEndianInputRec.TypSamplePeriod;
	pLMidB = pLSB + 1;
	pMMidB = pLMidB + 1;
	pMSB = pMMidB + 1;
	GUtils::OSConvertBytesToFloat(*pLSB, *pLMidB, *pMMidB, *pMSB, &(pPlatformEndianOutputRec->TypSamplePeriod));

	pLSB = (unsigned char *) &littleEndianInputRec.TypNumberofSamples;
	pMSB = pLSB + 1;
	GUtils::OSConvertBytesToShort(*pLSB, *pMSB, (short *) &(pPlatformEndianOutputRec->TypNumberofSamples));

	pLSB = (unsigned char *) &littleEndianInputRec.WarmUpTime;
	pMSB = pLSB + 1;
	GUtils::OSConvertBytesToShort(*pLSB, *pMSB, (short *) &(pPlatformEndianOutputRec->WarmUpTime));

	pLSB = (unsigned char *) &littleEndianInputRec.YminValue;
	pLMidB = pLSB + 1;
	pMMidB = pLMidB + 1;
	pMSB = pMMidB + 1;
	GUtils::OSConvertBytesToFloat(*pLSB, *pLMidB, *pMMidB, *pMSB, &(pPlatformEndianOutputRec->YminValue));

	pLSB = (unsigned char *) &littleEndianInputRec.YmaxValue;
	pLMidB = pLSB + 1;
	pMMidB = pLMidB + 1;
	pMSB = pMMidB + 1;
	GUtils::OSConvertBytesToFloat(*pLSB, *pLMidB, *pMMidB, *pMSB, &(pPlatformEndianOutputRec->YmaxValue));

	unsigned int numCalPages = pPlatformEndianOutputRec->HighestValidCalPageIndex + 1;
	if (numCalPages > 3)
	{
		numCalPages = 3;
		pPlatformEndianOutputRec->HighestValidCalPageIndex = numCalPages - 1;
	}

	for (unsigned int i = 0; i < numCalPages; i++)
	{
		pLSB = (unsigned char *) &(littleEndianInputRec.CalibrationPage[i].CalibrationCoefficientA);
		pLMidB = pLSB + 1;
		pMMidB = pLMidB + 1;
		pMSB = pMMidB + 1;
		GUtils::OSConvertBytesToFloat(*pLSB, *pLMidB, *pMMidB, *pMSB, &(pPlatformEndianOutputRec->CalibrationPage[i].CalibrationCoefficientA));

		pLSB = (unsigned char *) &(littleEndianInputRec.CalibrationPage[i].CalibrationCoefficientB);
		pLMidB = pLSB + 1;
		pMMidB = pLMidB + 1;
		pMSB = pMMidB + 1;
		GUtils::OSConvertBytesToFloat(*pLSB, *pLMidB, *pMMidB, *pMSB, &(pPlatformEndianOutputRec->CalibrationPage[i].CalibrationCoefficientB));

		pLSB = (unsigned char *) &(littleEndianInputRec.CalibrationPage[i].CalibrationCoefficientC);
		pLMidB = pLSB + 1;
		pMMidB = pLMidB + 1;
		pMSB = pMMidB + 1;
		GUtils::OSConvertBytesToFloat(*pLSB, *pLMidB, *pMMidB, *pMSB, &(pPlatformEndianOutputRec->CalibrationPage[i].CalibrationCoefficientC));
	}
}

void GMBLSensor::MarshallDDSRec(
	GSensorDDSRec *pLittleEndianOutputRec,			//[o] Store the rec here in little endian format - data is stored on the sensor in this format.
	const GSensorDDSRec &platformEndianInputRec)	//[i] Input record is in processor specific endian format.
{
	//Note that marshalling should not affect the checksum because the set of bytes in the structure does not change, 
	//just the order of the bytes in the structure. The XOR operation used to calculate the checksum is commutative
	//and associative, so changing the order of the bytes should not effect the outcome.
	*pLittleEndianOutputRec = platformEndianInputRec;

	unsigned char * pLSB;
	unsigned char * pLMidB;
	unsigned char * pMMidB;
	unsigned char * pMSB;

	pLSB = (unsigned char *) &(pLittleEndianOutputRec->MinSamplePeriod);
	pLMidB = pLSB + 1;
	pMMidB = pLMidB + 1;
	pMSB = pMMidB + 1;
	GUtils::OSConvertFloatToBytes(platformEndianInputRec.MinSamplePeriod, pLSB, pLMidB, pMMidB, pMSB);

	pLSB = (unsigned char *) &(pLittleEndianOutputRec->TypSamplePeriod);
	pLMidB = pLSB + 1;
	pMMidB = pLMidB + 1;
	pMSB = pMMidB + 1;
	GUtils::OSConvertFloatToBytes(platformEndianInputRec.TypSamplePeriod, pLSB, pLMidB, pMMidB, pMSB);

	pLSB = (unsigned char *) &(pLittleEndianOutputRec->TypNumberofSamples);
	pMSB = pLSB + 1;
	GUtils::OSConvertShortToBytes(platformEndianInputRec.TypNumberofSamples, pLSB, pMSB);

	pLSB = (unsigned char *) &(pLittleEndianOutputRec->WarmUpTime);
	pMSB = pLSB + 1;
	GUtils::OSConvertShortToBytes(platformEndianInputRec.WarmUpTime, pLSB, pMSB);

	pLSB = (unsigned char *) &(pLittleEndianOutputRec->YminValue);
	pLMidB = pLSB + 1;
	pMMidB = pLMidB + 1;
	pMSB = pMMidB + 1;
	GUtils::OSConvertFloatToBytes(platformEndianInputRec.YminValue, pLSB, pLMidB, pMMidB, pMSB);

	pLSB = (unsigned char *) &(pLittleEndianOutputRec->YmaxValue);
	pLMidB = pLSB + 1;
	pMMidB = pLMidB + 1;
	pMSB = pMMidB + 1;
	GUtils::OSConvertFloatToBytes(platformEndianInputRec.YmaxValue, pLSB, pLMidB, pMMidB, pMSB);

	unsigned int numCalPages = platformEndianInputRec.HighestValidCalPageIndex + 1;
	if (numCalPages > 3)
	{
		numCalPages = 3;
		pLittleEndianOutputRec->HighestValidCalPageIndex = numCalPages - 1;
	}

	for (unsigned int i = 0; i < numCalPages; i++)
	{
		pLSB = (unsigned char *) &(pLittleEndianOutputRec->CalibrationPage[i].CalibrationCoefficientA);
		pLMidB = pLSB + 1;
		pMMidB = pLMidB + 1;
		pMSB = pMMidB + 1;
		GUtils::OSConvertFloatToBytes(platformEndianInputRec.CalibrationPage[i].CalibrationCoefficientA, pLSB, pLMidB, pMMidB, pMSB);

		pLSB = (unsigned char *) &(pLittleEndianOutputRec->CalibrationPage[i].CalibrationCoefficientB);
		pLMidB = pLSB + 1;
		pMMidB = pLMidB + 1;
		pMSB = pMMidB + 1;
		GUtils::OSConvertFloatToBytes(platformEndianInputRec.CalibrationPage[i].CalibrationCoefficientB, pLSB, pLMidB, pMMidB, pMSB);

		pLSB = (unsigned char *) &(pLittleEndianOutputRec->CalibrationPage[i].CalibrationCoefficientC);
		pLMidB = pLSB + 1;
		pMMidB = pLMidB + 1;
		pMSB = pMMidB + 1;
		GUtils::OSConvertFloatToBytes(platformEndianInputRec.CalibrationPage[i].CalibrationCoefficientC, pLSB, pLMidB, pMMidB, pMSB);
	}
}

cppstring GMBLSensor::GetUnits(void)
{
	cppstring s;
	int nPage = m_sensorDDSRec.ActiveCalPage;
	if (nPage > m_sensorDDSRec.HighestValidCalPageIndex)
		nPage = 0;
	s = cppstring(&(m_sensorDDSRec.CalibrationPage[nPage].Units[0]), 
			sizeof(m_sensorDDSRec.CalibrationPage[nPage].Units));

	return s;
}

real GMBLSensor::CalibrateData(real fRawVolts)
{
	real fCalibratedMeasurement;

	int nPage = m_sensorDDSRec.ActiveCalPage;
	if (nPage > m_sensorDDSRec.HighestValidCalPageIndex)
		nPage = 0;
	GCalibrationPage *pActiveCalibration = &(m_sensorDDSRec.CalibrationPage[nPage]);

	switch (m_sensorDDSRec.CalibrationEquation)
	{
		case kEquationType_Linear:
			fCalibratedMeasurement = CalibrateData_Linear(fRawVolts, pActiveCalibration->CalibrationCoefficientA, 
				pActiveCalibration->CalibrationCoefficientB);
			break;
		case kEquationType_Quadratic:
			fCalibratedMeasurement = CalibrateData_Quadratic(fRawVolts, pActiveCalibration->CalibrationCoefficientA, 
				pActiveCalibration->CalibrationCoefficientB, pActiveCalibration->CalibrationCoefficientC);
			break;
		case kEquationType_ModifiedPower:
			fCalibratedMeasurement = CalibrateData_ModifiedPower(fRawVolts, pActiveCalibration->CalibrationCoefficientA, 
				pActiveCalibration->CalibrationCoefficientB);
			break;
		case kEquationType_SteinhartHart:
			{
				char unit = pActiveCalibration->Units[0];
				if ('(' == unit)
					unit = pActiveCalibration->Units[1];
				fCalibratedMeasurement = CalibrateData_SteinhartHart(fRawVolts, pActiveCalibration->CalibrationCoefficientA, 
					pActiveCalibration->CalibrationCoefficientB, pActiveCalibration->CalibrationCoefficientC,
					15000.0, 5.0, unit);
			}
			break;
		default:
			fCalibratedMeasurement = fRawVolts;
			break;
	}

	return fCalibratedMeasurement;
}

#ifdef LIB_NAMESPACE
}
#endif

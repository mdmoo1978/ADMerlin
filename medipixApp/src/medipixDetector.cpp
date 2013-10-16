/* medipixDetector.cpp
 *
 * This is a driver for a Medipix 3 detector chip.
 *
 * The driver is designed to communicate with the chip via the matching Labview controller over TCP/IP
 *
 * Author: Giles Knap
 *         Diamond Light Source Ltd.
 *
 * Created:  Jan 06 2012
 *

 * Original Source from pilatusDetector by Mark Rivers
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdint.h>

// #include <epicsTime.h>
#include <epicsThread.h>
#include <epicsEvent.h>
#include <epicsMutex.h>
#include <epicsString.h>
#include <epicsStdio.h>
#include <epicsMutex.h>
#include <cantProceed.h>
#include <iocsh.h>
#include <epicsExport.h>

#include <asynOctetSyncIO.h>

#include "ADDriver.h"

#include "mpxConnection.h"
#include "medipixDetector.h"

#define MAX(a,b) a>b ? a : b

/** helper functions for endian conversion
 *
 */
inline void medipixDetector::endian_swap(unsigned short& x)
{
    if(detType != Merlin)
        return;
    x = (x >> 8) | (x << 8);
}

inline void medipixDetector::endian_swap(unsigned int& x)
{
    if(detType != Merlin)
        return;
    x = (x >> 24) | ((x << 8) & 0x00FF0000) | ((x >> 8) & 0x0000FF00)
            | (x << 24);
}

inline void medipixDetector::endian_swap(uint64_t& x)
{
    if(detType != Merlin)
        return;
    x = ((((x) & 0x00000000000000FFLL) << 0x38)
            | (((x) & 0x000000000000FF00LL) << 0x28)
            | (((x) & 0x0000000000FF0000LL) << 0x18)
            | (((x) & 0x00000000FF000000LL) << 0x08)
            | (((x) & 0x000000FF00000000LL) >> 0x08)
            | (((x) & 0x0000FF0000000000LL) >> 0x18)
            | (((x) & 0x00FF000000000000LL) >> 0x28)
            | (((x) & 0xFF00000000000000LL) >> 0x38));
}

void medipixDetector::fromLabViewStr(const char *str)
{
    setStringParam(ADStringFromServer, str);
}

void medipixDetector::toLabViewStr(const char *str)
{
    setStringParam(ADStringToServer, str);
}

/** Helper function to copy a 16 bit buffer into an NDArray
 *
 */
NDArray* medipixDetector::copyToNDArray16(size_t *dims, char *buffer)
{
    // copy the data into NDArray, switching to little endien and
    // Inverting in the Y axis (medipix origin is at bottom left)
    epicsUInt16 *pData, *pSrc;
    size_t x, y;

    NDArray* pImage = this->pNDArrayPool->alloc(2, dims, NDUInt16, 0, NULL);

    if (pImage == NULL)
    {
        asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: unable to allocate NDArray from pool\n", driverName,
                "copyToNDArray16");
        setStringParam(ADStatusMessage,
                "Error: run out of buffers in detector driver");
    }
    else
    {
        for (y = 0; y < dims[1]; y++)
        {
            for (x = 0, pData = (epicsUInt16 *) pImage->pData + y * dims[0], pSrc =
                    (epicsUInt16 *) (buffer + MPX_IMG_HDR_LEN)
                            + (dims[1] - y) * dims[0]; x < dims[0];
                    x++, pData++, pSrc++)
            {
                *pData = *pSrc;
                endian_swap(*pData);
            }
        }
    }
    return pImage;
}

/** Helper function to copy a 32 bit buffer into an NDArray
 *
 */
NDArray* medipixDetector::copyToNDArray32(size_t* dims, char* buffer)
{
    epicsUInt32 *pData, *pSrc;
    size_t x, y;

    NDArray* pImage = this->pNDArrayPool->alloc(2, dims, NDUInt32, 0, NULL);

    if (pImage == NULL)
    {
        asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: unable to allocate NDArray from pool\n", driverName,
                "copyToNDArray32");
        setStringParam(ADStatusMessage,
                "Error: run out of buffers in detector driver");
    }
    else
    {
        for (y = 0; y < dims[1]; y++)
        {
            for (x = 0, pData = (epicsUInt32 *) pImage->pData + y * dims[0], pSrc =
                    (epicsUInt32 *) (buffer + MPX_IMG_HDR_LEN)
                            + (dims[1] - y) * dims[0]; x < dims[0];
                    x++, pData++, pSrc++)
            {
                *pData = *pSrc;
                endian_swap(*pData);
            }
        }
    }
    return pImage;
}
asynStatus medipixDetector::setModeCommands(int function)
{
    asynStatus status;
    char value[MPX_MAXLINE];
    int counter1Enabled, continuousEnabled;

    if (function == medipixEnableCounter1)
    {
        status = getIntegerParam(medipixEnableCounter1, &counter1Enabled);
        if ((status != asynSuccess)
                || (counter1Enabled < 0 || counter1Enabled > 1))
        {
            counter1Enabled = 0;
            setIntegerParam(medipixEnableCounter1, counter1Enabled);
        }
        epicsSnprintf(value, MPX_MAXLINE, "%d", counter1Enabled);
        cmdConnection->mpxSet(MPXVAR_ENABLECOUNTER1, value,
                Labview_DEFAULT_TIMEOUT);
    }

    if (function == medipixContinuousRW)
    {
        status = getIntegerParam(medipixContinuousRW, &continuousEnabled);
        if ((status != asynSuccess)
                || (continuousEnabled < 0 || continuousEnabled > 1))
        {
            continuousEnabled = 0;
            setIntegerParam(medipixContinuousRW, continuousEnabled);
        }
        epicsSnprintf(value, MPX_MAXLINE, "%d", continuousEnabled);
        cmdConnection->mpxSet(MPXVAR_CONTINUOUSRW, value,
                Labview_DEFAULT_TIMEOUT);
    }

    epicsThreadSleep(.01);

    // now get the values again from the device -- it may reset them to consistent values
    // (presently only one of medipixContinuousRW or medipixEnableCounter1 can be set at a time)
    status = cmdConnection->mpxGet(MPXVAR_ENABLECOUNTER1,
            Labview_DEFAULT_TIMEOUT);
    if (status == asynSuccess)
        setIntegerParam(medipixEnableCounter1,
                atoi(cmdConnection->fromLabviewValue));

    status = cmdConnection->mpxGet(MPXVAR_CONTINUOUSRW,
            Labview_DEFAULT_TIMEOUT);
    if (status == asynSuccess)
        setIntegerParam(medipixContinuousRW,
                atoi(cmdConnection->fromLabviewValue));

    return (asynSuccess);
}

asynStatus medipixDetector::setAcquireParams()
{
    int triggerMode;
    char value[MPX_MAXLINE];
    asynStatus status;
//	char *substr = NULL;
//	int pixelCutOff = 0;

    // avoid chatty startup which keeps setting these values
    if (startingUp)
        return asynSuccess;

    int numImages;
    status = getIntegerParam(ADNumImages, &numImages);
    if ((status != asynSuccess) || (numImages < 1))
    {
        numImages = 1;
        setIntegerParam(ADNumImages, numImages);
    }

    int numExposures;
    status = getIntegerParam(ADNumExposures, &numExposures);
    if ((status != asynSuccess) || (numExposures < 1))
    {
        numExposures = 1;
        setIntegerParam(ADNumExposures, numExposures);
    }

    int counterDepth;
    status = getIntegerParam(medipixCounterDepth, &counterDepth);
    if ((status != asynSuccess) || (counterDepth != 12 && counterDepth != 24)) // currently limited to 12/24 bit
    {
        counterDepth = 12;
        setIntegerParam(medipixCounterDepth, counterDepth);
    }

    double acquireTime;
    status = getDoubleParam(ADAcquireTime, &acquireTime);
    if ((status != asynSuccess) || (acquireTime < 0.))
    {
        acquireTime = 1.;
        setDoubleParam(ADAcquireTime, acquireTime);
    }

    double acquirePeriod;
    status = getDoubleParam(ADAcquirePeriod, &acquirePeriod);
    if ((status != asynSuccess) || (acquirePeriod < 0.))
    {
        acquirePeriod = 1.0;
        setDoubleParam(ADAcquirePeriod, acquirePeriod);
    }
    callParamCallbacks();

    // set the values enmasse - an attempt to fix the strange GUI updates caused by slow comms (failed)
    epicsSnprintf(value, MPX_MAXLINE, "%d", numExposures);
    cmdConnection->mpxSet(MPXVAR_NUMFRAMESPERTRIGGER, value,
            Labview_DEFAULT_TIMEOUT);
    epicsSnprintf(value, MPX_MAXLINE, "%d", counterDepth);
    cmdConnection->mpxSet(MPXVAR_COUNTERDEPTH, value, Labview_DEFAULT_TIMEOUT);
    epicsSnprintf(value, MPX_MAXLINE, "%f", acquireTime * 1000); // translated into millisec
    cmdConnection->mpxSet(MPXVAR_ACQUISITIONTIME, value,
            Labview_DEFAULT_TIMEOUT);
    epicsSnprintf(value, MPX_MAXLINE, "%f", acquirePeriod * 1000); // translated into millisec
    cmdConnection->mpxSet(MPXVAR_ACQUISITIONPERIOD, value,
            Labview_DEFAULT_TIMEOUT);

    status = getIntegerParam(ADTriggerMode, &triggerMode);
    if (status != asynSuccess)
        triggerMode = TMInternal;
    // medipix individually controls how start and stop triggers are read
    // here we translate the chosen trigger mode into a combination of start
    // and stop modes
    switch (triggerMode)
    {
    case TMInternal:
        cmdConnection->mpxSet(MPXVAR_TRIGGERSTART, TMTrigInternal,
                Labview_DEFAULT_TIMEOUT);
        cmdConnection->mpxSet(MPXVAR_TRIGGERSTOP, TMTrigInternal,
                Labview_DEFAULT_TIMEOUT);
        break;
    case TMExternalEnable:
        cmdConnection->mpxSet(MPXVAR_TRIGGERSTART, TMTrigRising,
                Labview_DEFAULT_TIMEOUT);
        cmdConnection->mpxSet(MPXVAR_TRIGGERSTOP, TMTrigFalling,
                Labview_DEFAULT_TIMEOUT);
        break;
    case TMExternalTriggerLow:
        cmdConnection->mpxSet(MPXVAR_TRIGGERSTART, TMTrigFalling,
                Labview_DEFAULT_TIMEOUT);
        cmdConnection->mpxSet(MPXVAR_TRIGGERSTOP, TMTrigInternal,
                Labview_DEFAULT_TIMEOUT);
        break;
    case TMExternalTriggerHigh:
        cmdConnection->mpxSet(MPXVAR_TRIGGERSTART, TMTrigRising,
                Labview_DEFAULT_TIMEOUT);
        cmdConnection->mpxSet(MPXVAR_TRIGGERSTOP, TMTrigInternal,
                Labview_DEFAULT_TIMEOUT);
        break;
    case TMExternalTriggerRising:
        cmdConnection->mpxSet(MPXVAR_TRIGGERSTART, TMTrigRising,
                Labview_DEFAULT_TIMEOUT);
        cmdConnection->mpxSet(MPXVAR_TRIGGERSTOP, TMTrigRising,
                Labview_DEFAULT_TIMEOUT);
        break;
    case TMSoftwareTrigger:
        cmdConnection->mpxSet(MPXVAR_TRIGGERSTART, TMTrigSoftware,
                Labview_DEFAULT_TIMEOUT);
        break;
    }

    // read the acquire period back from the server so that it can insert
    // the readback time if necessary
    cmdConnection->mpxGet(MPXVAR_ACQUISITIONPERIOD, Labview_DEFAULT_TIMEOUT);
    if (status == asynSuccess)
        setDoubleParam(ADAcquirePeriod,
                atof(cmdConnection->fromLabviewValue) / 1000); // translated into secs

    return (asynSuccess);

}

asynStatus medipixDetector::getThreshold()
{
    int status;

    if (startingUp)
        return asynSuccess;

    /* Read back the actual setting, in case we are out of bounds.*/
    status = cmdConnection->mpxGet(MPXVAR_THRESHOLD0, Labview_DEFAULT_TIMEOUT);
    if (status == asynSuccess)
        setDoubleParam(medipixThreshold0,
                atof(cmdConnection->fromLabviewValue));
    status = cmdConnection->mpxGet(MPXVAR_THRESHOLD1, Labview_DEFAULT_TIMEOUT);
    if (status == asynSuccess)
        setDoubleParam(medipixThreshold1,
                atof(cmdConnection->fromLabviewValue));
    status = cmdConnection->mpxGet(MPXVAR_OPERATINGENERGY,
            Labview_DEFAULT_TIMEOUT);
    if (status == asynSuccess)
        setDoubleParam(medipixOperatingEnergy,
                atof(cmdConnection->fromLabviewValue));

    callParamCallbacks();

    return (asynSuccess);
}

asynStatus medipixDetector::updateThresholdScanParms()
{
    asynStatus status = asynSuccess;
    char valueStr[MPX_MAXLINE];
    int thresholdScan;
    double start, stop, step;

    if (startingUp)
        return asynSuccess;

    getDoubleParam(medipixStartThresholdScan, &start);
    getDoubleParam(medipixStopThresholdScan, &stop);
    getDoubleParam(medipixStepThresholdScan, &step);
    getIntegerParam(medipixThresholdScan, &thresholdScan);

    epicsSnprintf(valueStr, MPX_MAXLINE, "%f", start);
    status = cmdConnection->mpxSet(MPXVAR_THSTART, valueStr,
            Labview_DEFAULT_TIMEOUT);

    if (status == asynSuccess)
    {
        epicsSnprintf(valueStr, MPX_MAXLINE, "%f", stop);
        status = cmdConnection->mpxSet(MPXVAR_THSTOP, valueStr,
                Labview_DEFAULT_TIMEOUT);
    }
    if (status == asynSuccess)
    {
        epicsSnprintf(valueStr, MPX_MAXLINE, "%f", step);
        status = cmdConnection->mpxSet(MPXVAR_THSTEP, valueStr,
                Labview_DEFAULT_TIMEOUT);
    }
    if (status == asynSuccess)
    {
        epicsSnprintf(valueStr, MPX_MAXLINE, "%d", thresholdScan);
        status = cmdConnection->mpxSet(MPXVAR_THSSCAN, valueStr,
                Labview_DEFAULT_TIMEOUT);
    }

    /* Read back the actual setting, in case we are out of bounds.*/
    status = cmdConnection->mpxGet(MPXVAR_THSTART, Labview_DEFAULT_TIMEOUT);
    if (status == asynSuccess)
        setDoubleParam(medipixStartThresholdScan,
                atof(cmdConnection->fromLabviewValue));
    status = cmdConnection->mpxGet(MPXVAR_THSTEP, Labview_DEFAULT_TIMEOUT);
    if (status == asynSuccess)
        setDoubleParam(medipixStepThresholdScan,
                atof(cmdConnection->fromLabviewValue));
    status = cmdConnection->mpxGet(MPXVAR_THSTOP, Labview_DEFAULT_TIMEOUT);
    if (status == asynSuccess)
        setDoubleParam(medipixStopThresholdScan,
                atof(cmdConnection->fromLabviewValue));
    status = cmdConnection->mpxGet(MPXVAR_THSSCAN, Labview_DEFAULT_TIMEOUT);
    if (status == asynSuccess)
        setIntegerParam(medipixThresholdScan,
                atoi(cmdConnection->fromLabviewValue));

    return status;
}

static void medipixTaskC(void *drvPvt)
{
    medipixDetector *pPvt = (medipixDetector *) drvPvt;

    pPvt->medipixTask();
}

/** This thread controls acquisition, reads image files to get the image data, and
 * does the callbacks to send it to higher layers
 * It is totally decoupled from the command thread and simply waits for data
 * frames to be sent on the data channel (TCP) regardless of the state in the command
 * thread and TCP channel */
void medipixDetector::medipixTask()
{
    int status = asynSuccess;
    int imageCounter;      // number of ndarrays sent to plugins
    int numImagesCounter;  // number of images received
    int counterDepth;
    NDArray *pImage;
    epicsTimeStamp startTime;
    const char *functionName = "medipixTask";
    size_t dims[2], dummy;
    size_t profileDims[2];
    int arrayCallbacks;
    int dummy2;
    int nread;
    char *bigBuff;
    char aquisitionHeader[MPX_ACQUISITION_HEADER_LEN + 1];
    int triggerMode;
    NDAttributeList *imageAttr = new NDAttributeList();

    // do not enter this thread until the IOC is initialised. This is because we are getting blocks of
    // data on the data channel at startup after we have had a buffer overrun
    while (startingUp)
    {
        epicsThreadSleep(.5);
    }

    this->lock();

    // allocate a buffer for reading in images from labview over network
    bigBuff = (char*) calloc(MPX_IMG_FRAME_LEN24, 1);

    /* Loop forever */
    while (1)
    {
        // Get the current time
        epicsTimeGetCurrent(&startTime);

        // Acquire an image from the data channel
        memset(bigBuff, 0, MPX_IMG_FRAME_LEN);

        /* We release the mutex when waiting because this takes a long time and
         * we need to allow abort operations to get through */
        this->unlock();

        // wait for the next data frame packet - this function spends most of its time here
        status = cmdConnection->mpxRead(this->pasynLabViewData, bigBuff,
        MPX_IMG_FRAME_LEN24, &nread, 10);
        this->lock();

        /* If there was an error jump to bottom of loop */
        if (status)
        {
            if (status == asynTimeout)
                status = asynSuccess;   // timeouts are expected
            else
            {
                asynPrint(this->pasynLabViewData, ASYN_TRACE_ERROR,
                        "%s:%s: error in Labview data channel response, status=%d\n",
                        driverName, functionName, status);
                setStringParam(ADStatusMessage,
                        "Error in Labview data channel response");
                // wait before trying again - otherwise socket error creates a tight loop
                epicsThreadSleep(.5);
            }
            continue;
        }

        asynPrint(this->pasynUserSelf, ASYN_TRACE_MPX,
                "\nReceived image frame of %d bytes\n", nread);

        if (pasynTrace->getTraceMask((pasynUserSelf))
                & (ASYN_TRACE_MPX_VERBOSE))
        {
            dataConnection->dumpData(bigBuff, nread);
        }

        medipixDataHeader header = dataConnection->parseDataHeader(bigBuff);
        if (header != MPXAcquisitionHeader)
        {
            getIntegerParam(ADNumImagesCounter, &numImagesCounter);
            numImagesCounter++;
            setIntegerParam(ADNumImagesCounter, numImagesCounter);
            if (imagesRemaining > 0)
                imagesRemaining--;
        }

        getIntegerParam(NDArrayCallbacks, &arrayCallbacks);

        if (arrayCallbacks)
        {
            getIntegerParam(medipixCounterDepth, &counterDepth);
            getIntegerParam(NDArrayCounter, &imageCounter);
            imageCounter++;
            setIntegerParam(NDArrayCounter, imageCounter);

            int idim;
            /* Get an image buffer from the pool */
            getIntegerParam(ADMaxSizeX, &idim);
            dims[0] = idim;
            getIntegerParam(ADMaxSizeY, &idim);
            dims[1] = idim;
            profileDims[0] = MAX(dims[0], dims[1]);
            profileDims[1] = 2;

            if (header == MPXAcquisitionHeader)
            {
                // this is an acquisition header
                strncpy(aquisitionHeader, bigBuff, MPX_ACQUISITION_HEADER_LEN);
                aquisitionHeader[MPX_ACQUISITION_HEADER_LEN] = 0;
            }
            else if (header == MPXDataHeader12)
            {
                asynPrint(this->pasynUserSelf, ASYN_TRACE_MPX,
                        "Creating a 12bit Array\n");

                pImage = copyToNDArray16(dims, bigBuff);
                if (pImage == NULL)
                    continue;
                dataConnection->parseDataFrame(pImage->pAttributeList, bigBuff,
                        header, &dummy, &dummy, &dummy2, &dummy2);
            }
            else if (header == MPXDataHeader24)
            {
                asynPrint(this->pasynUserSelf, ASYN_TRACE_MPX,
                        "Creating a 24bit Array\n");

                pImage = copyToNDArray32(dims, bigBuff);
                if (pImage == NULL)
                    continue;
                dataConnection->parseDataFrame(pImage->pAttributeList, bigBuff,
                        header, &dummy, &dummy, &dummy2, &dummy2);
            }
            else if (header == MPXGenericImageHeader)
            {
                int pixelSize;
                asynPrint(this->pasynUserSelf, ASYN_TRACE_MPX,
                        "Creating a generic Image NDArray\n");

                // Parse the header and use the information to determine the
                // size of the NDArray
                imageAttr->clear();
                dataConnection->parseDataFrame(imageAttr, bigBuff, header,
                        &(dims[0]), &(dims[1]), &pixelSize, &dummy2);
                pImage = NULL;
                if (pixelSize == 16)
                {
                    pImage = copyToNDArray16(dims, bigBuff);
                }
                else if (pixelSize == 32)
                {
                    pImage = copyToNDArray32(dims, bigBuff);
                }
                if (pImage == NULL)
                    continue;
                imageAttr->copy(pImage->pAttributeList);
            }
            else if (header == MPXProfileHeader12
                    || header == MPXProfileHeader24)
            {
                asynPrint(this->pasynUserSelf, ASYN_TRACE_MPX,
                        "Creating an Array for Profile Type %d\n", header);

                // for profiles we do a max dim * 2 array to hold both x
                // and y profile
                pImage = this->pNDArrayPool->alloc(2, profileDims, NDUInt32, 0,
                        NULL);

                if (pImage == NULL)
                {
                    asynPrint(this->pasynLabViewData, ASYN_TRACE_ERROR,
                            "%s:%s: unable to allocate NDArray from pool\n",
                            driverName, "copyToNDArray32");
                    setStringParam(ADStatusMessage,
                            "Error: run out of buffers in detector driver");
                    continue;
                }

                int profileMask = 0;

                dataConnection->parseDataFrame(pImage->pAttributeList, bigBuff,
                        header, &dummy, &dummy, &dummy2, &profileMask);

                if (profileMask
                        != (MPXPROFILES_XPROFILE | MPXPROFILES_YPROFILE
                                | MPXPROFILES_SUM))
                {
                    asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                            "%s:%s: unsupported PROFILES mode %d\n", driverName,
                            functionName, profileMask);
                }
                else
                {
                    epicsUInt32 *pData;
                    epicsUInt32 *pWaveForm;
                    uint64_t *pSrc;
                    size_t x;
                    int y;

                    asynPrint(this->pasynUserSelf, ASYN_TRACE_MPX,
                            "%s:%s: reading PROFILES data%d\n", driverName,
                            functionName, profileMask);

                    // Copy the X,Y profile data into the (size * 2) NDArray
                    // and into the X,Y waveforms
                    pData = (epicsUInt32*) pImage->pData;
                    for (x = 0, pWaveForm = (epicsUInt32 *) profileX, pSrc =
                            (uint64_t *) ((bigBuff + MPX_IMG_HDR_LEN));
                            x < dims[0]; x++, pWaveForm++, pSrc++, pData++)
                    {
                        //printf("%llu ", *pSrc);
                        endian_swap(*pSrc);
                        *pWaveForm = (epicsUInt32) *pSrc;
                        *pData = (epicsUInt32) *pSrc;
                    }
                    // Invert the Y profile (medipix origin is at bottom left)
                    for (y = dims[1] - 1, pWaveForm = (epicsUInt32 *) profileY, pSrc =
                            (uint64_t *) ((bigBuff + MPX_IMG_HDR_LEN
                                    + MPX_PROFILE_LEN)); y >= 0;
                            y--, pWaveForm++, pSrc++, pData++)
                    {
                        //printf("%d-%llu ", y, *pSrc);
                        endian_swap(*pSrc);
                        *pWaveForm = (epicsUInt32) *pSrc;
                        *pData = (epicsUInt32) *pSrc;
                    }

                    doCallbacksInt32Array(profileY, dims[1], medipixProfileY,
                            0);
                    doCallbacksInt32Array(profileX, dims[0], medipixProfileX,
                            0);
                }
            }
            else
            {
                asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                        "Unknown header type %d\n", header);
            }

            // for Data frames - complete the NDAttributes, pass the NDArray on
            if (header == MPXDataHeader12 || header == MPXDataHeader24
                    || header == MPXProfileHeader12
                    || header == MPXProfileHeader24)
            {
                // Put the frame number and time stamp into the buffer
                pImage->uniqueId = imageCounter;
                pImage->timeStamp = startTime.secPastEpoch
                        + startTime.nsec / 1.e9;

                // string attributes are global in HDF5 plugin so the most recent
                // acquisition header is applied to all files
                pImage->pAttributeList->add("Acquisition Header", "",
                        NDAttrString, aquisitionHeader);

                /* Get any attributes that have been defined for this driver */
                this->getAttributes(pImage->pAttributeList);

                // Call the NDArray callback
                // Must release the lock here, to avoid a deadlock: we can
                // block on the plugin lock, and the plugin can be calling us
                this->unlock();
                if (header == MPXDataHeader12 || header == MPXDataHeader24)
                {
                    doCallbacksGenericPointer(pImage, NDArrayData, 0);
                }
                else
                {
                    // address 1 on the port is used for profiles
                    // TODO use of port 1 is not working in NDPluginBase so
                    // currently reverting to use the same address
                    // (i.e. setting Medipix1:ROI:NDArrayAddress has no effect
                    doCallbacksGenericPointer(pImage, NDArrayData, 0);
                }
                this->lock();

                /* Free the image buffer */
                pImage->release();
            }
        }

        // If we are using SW triggers then reset the trigger to 0 when an image is
        // received
        status = getIntegerParam(ADTriggerMode, &triggerMode);
        if (triggerMode == TMSoftwareTrigger)
        {
            // software trigger resets  when image received
            setIntegerParam(medipixSoftwareTrigger, 0);
        }

        // If all the expected images have been received then the driver can
        // complete the acquisition and return to waiting for acquisition state
        if (imagesRemaining == 0)
        {
            setIntegerParam(ADAcquire, 0);
            setIntegerParam(ADStatus, ADStatusIdle);
        }

        /* Call the callbacks to update any changes */
        callParamCallbacks();
    }
    // release the image buffer (in reality this does not get called
    // We need a thread shutdown signal)
    free(bigBuff);
}

static void medipixStatusC(void *drvPvt)
{
    medipixDetector *pPvt = (medipixDetector *) drvPvt;

    pPvt->medipixStatus();
}

/** This thread periodically read the detector status (temperature, humidity, etc.)
 It does not run if we are acquiring data, to avoid polling Labview when taking data.*/
void medipixDetector::medipixStatus()
{
    int result = asynSuccess;
    int status = 0;
    int statusCode;

// let the startup script complete before attempting I/O
    epicsThreadSleep(4);
    startingUp = 0;

    this->lock();

// make sure important grouped variables are set to agree with
// IOCs auto saved values
    setAcquireParams();
    updateThresholdScanParms();
    getThreshold();

    result = cmdConnection->mpxGet(MPXVAR_GETSOFTWAREVERSION,
            Labview_DEFAULT_TIMEOUT);
    statusCode = atoi(cmdConnection->fromLabviewValue);

// initial status
    setIntegerParam(ADStatus, ADStatusIdle);

    this->unlock();

    while (1)
    {
        epicsThreadSleep(4);
        this->lock();
        getIntegerParam(ADStatus, &status);

        if (status == ADStatusIdle)
        {
            setStringParam(ADStatusMessage, "Waiting for acquire command");
            callParamCallbacks();
        }
        this->unlock();
    }

}

/** Called when asyn clients call pasynInt32->write().
 * This function performs actions for some parameters, including ADAcquire, ADTriggerMode, etc.
 * For all parameters it sets the value in the parameter library and calls any registered callbacks..
 * \param[in] pasynUser pasynUser structure that encodes the reason and address.
 * \param[in] value Value to write. */
asynStatus medipixDetector::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
    char strVal[MPX_MAXLINE];
    int function = pasynUser->reason;
    int adstatus;
    int counter1Enabled;
    int imageMode, imagesToAcquire, profileMaskParm;
    asynStatus status = asynSuccess;
    const char *functionName = "writeInt32";

    status = setIntegerParam(function, value);

    if (function == medipixReset)
    {
        cmdConnection->mpxCommand(MPXCMD_RESET, Labview_DEFAULT_TIMEOUT);
// I cannot successfully reconnect to the server after a reset
// the only solution found so far is to restart the ioc
        exit(0);
    }
    else if (function == medipixSoftwareTrigger)
    {
        cmdConnection->mpxCommand(MPXCMD_SOFTWARETRIGGER,
                Labview_DEFAULT_TIMEOUT);
    }
    else if (function == ADAcquire)
    {
        getIntegerParam(ADStatus, &adstatus);
        if (value && (adstatus == ADStatusIdle || adstatus == ADStatusError))
        {
            setIntegerParam(ADStatus, ADStatusAcquire);
            setStringParam(ADStatusMessage, "Acquiring...");
            // reset the image count - this is then used to determine when acquisition is complete
            setIntegerParam(ADNumImagesCounter, 0);
            getIntegerParam(ADNumImages, &imagesToAcquire);
            // set number of images to acquire based on the capture mode
            getIntegerParam(ADImageMode, &imageMode);
            getIntegerParam(medipixProfileControl, &profileMaskParm);

            switch (imageMode)
            {
            case ADImageSingle:
                imagesRemaining = imagesToAcquire = 1;
                break;
            case ADImageMultiple:
                imagesRemaining = imagesToAcquire;
                break;
            case ADImageContinuous:
                imagesToAcquire = 0;
                imagesRemaining = -1;
                break;
            }

            status = getIntegerParam(medipixEnableCounter1, &counter1Enabled);
            if (counter1Enabled == 1 && imagesRemaining > 0)
                imagesRemaining *= 2;

            epicsSnprintf(strVal, MPX_MAXLINE, "%d", imagesToAcquire);
            cmdConnection->mpxSet(MPXVAR_NUMFRAMESTOACQUIRE, strVal,
                    Labview_DEFAULT_TIMEOUT);

            if (profileMaskParm & MPXPROFILES_IMAGE == MPXPROFILES_IMAGE)
            {
                cmdConnection->mpxCommand(MPXCMD_STARTACQUISITION,
                        Labview_DEFAULT_TIMEOUT);
            }
            else
            {
                cmdConnection->mpxCommand(MPXCMD_PROFILES,
                        Labview_DEFAULT_TIMEOUT);
            }
        }
        if (!value && (adstatus == ADStatusAcquire))
        {
            setIntegerParam(ADStatus, ADStatusIdle);
            cmdConnection->mpxCommand(MPXCMD_STOPACQUISITION,
                    Labview_DEFAULT_TIMEOUT);
        }
    }
    else if (function == medipixStartThresholdScanning)
    {
        getIntegerParam(ADStatus, &adstatus);
        if (value && (adstatus == ADStatusIdle || adstatus == ADStatusError))
        {
            // calculate the number of images expected
            double start, stop, step;
            getDoubleParam(medipixStartThresholdScan, &start);
            getDoubleParam(medipixStopThresholdScan, &stop);
            getDoubleParam(medipixStepThresholdScan, &step);
            imagesRemaining = (int) ((stop - start) / step);

            setIntegerParam(ADStatus, ADStatusAcquire);
            setIntegerParam(ADNumImagesCounter, 0);
            setStringParam(ADStatusMessage, "Performing Threshold Scan...");
            setIntegerParam(ADNumImages, 1); // internally Merlin does this so we set EPICS PV to match
            status = cmdConnection->mpxCommand(MPXCMD_THSCAN,
                    Labview_DEFAULT_TIMEOUT);
        }
    }
    else if ((function == ADTriggerMode) || (function == ADNumImages)
            || (function == ADNumExposures)
            || (function == medipixCounterDepth))
    {
        setAcquireParams();
    }
    else if ((function == medipixEnableCounter1
            || function == medipixContinuousRW))
    {
        setModeCommands(function);
    }
    else if (function == medipixThresholdApply)
    {
        getThreshold();
    }
    else if (function == medipixProfileControl)
    {
        epicsSnprintf(strVal, MPX_MAXLINE, "%d", value);
        cmdConnection->mpxSet(MPXCMD_PROFILECONTROL, strVal,
                Labview_DEFAULT_TIMEOUT);
        setIntegerParam(medipixProfileControl, value);
    }
    else
    {
// function numbers are assigned sequentially via createParam in the constructor and hence
// any function numbers lower than our first function is handled by a (the) super class
        if (function < FIRST_medipix_PARAM)
            status = ADDriver::writeInt32(pasynUser, value);
    }

    /* Do callbacks so higher layers see any changes */
    callParamCallbacks();

    if (status)
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
                "%s:%s: error, status=%d function=%d, value=%d\n", driverName,
                functionName, status, function, value);
    else
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER,
                "%s:%s: function=%d, value=%d\n", driverName, functionName,
                function, value);
    return status;
}

/** Called when asyn clients call pasynFloat64->write().
 * This function performs actions for some parameters, including ADAcquireTime, ADGain, etc.
 * For all parameters it sets the value in the parameter library and calls any registered callbacks..
 * \param[in] pasynUser pasynUser structure that encodes the reason and address.
 * \param[in] value Value to write. */
asynStatus medipixDetector::writeFloat64(asynUser *pasynUser,
        epicsFloat64 value)
{
    int function = pasynUser->reason;
    asynStatus status = asynSuccess;
    const char *functionName = "writeFloat64";
    char value_str[MPX_MAXLINE];
    double oldValue;

    /* Set the parameter and readback in the parameter library.  This may be overwritten when we read back the
     * status at the end, but that's OK */
    getDoubleParam(function, &oldValue);
    status = setDoubleParam(function, value);

    /* Changing any of the following parameters requires recomputing the base image */
    if (function == medipixThreshold0)
    {
        epicsSnprintf(value_str, MPX_MAXLINE, "%f", value);
        status = cmdConnection->mpxSet(MPXVAR_THRESHOLD0, value_str,
                Labview_DEFAULT_TIMEOUT);
        getThreshold();
    }
    else if (function == medipixThreshold1)
    {
        epicsSnprintf(value_str, MPX_MAXLINE, "%f", value);
        status = cmdConnection->mpxSet(MPXVAR_THRESHOLD1, value_str,
                Labview_DEFAULT_TIMEOUT);
        getThreshold();
    }
    else if (function == medipixOperatingEnergy)
    {
        epicsSnprintf(value_str, MPX_MAXLINE, "%f", value);
        status = cmdConnection->mpxSet(MPXVAR_OPERATINGENERGY, value_str,
                Labview_DEFAULT_TIMEOUT);
        getThreshold();
    }
    else if ((function == ADAcquireTime) || (function == ADAcquirePeriod))
    {
        setAcquireParams();
    }
    else if ((function == medipixStartThresholdScan)
            || (function == medipixStopThresholdScan)
            || (function == medipixStepThresholdScan))
    {
        updateThresholdScanParms();
    }
    else
    {
        /* If this parameter belongs to a base class call its method */
        if (function < FIRST_medipix_PARAM)
            status = ADDriver::writeFloat64(pasynUser, value);
    }

    if (status)
    {
        /* Something went wrong so we set the old value back */
        setDoubleParam(function, oldValue);
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
                "%s:%s error, status=%d function=%d, value=%f\n", driverName,
                functionName, status, function, value);
    }
    else
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER,
                "%s:%s: function=%d, value=%f\n", driverName, functionName,
                function, value);

    /* Do callbacks so higher layers see any changes */
    callParamCallbacks();
    return status;
}

/** Called when asyn clients call pasynOctet->write().
 * This function performs actions for some parameters, including medipixBadPixelFile, ADFilePath, etc.
 * For all parameters it sets the value in the parameter library and calls any registered callbacks..
 * \param[in] pasynUser pasynUser structure that encodes the reason and address.
 * \param[in] value Address of the string to write.
 * \param[in] nChars Number of characters to write.
 * \param[out] nActual Number of characters actually written. */
asynStatus medipixDetector::writeOctet(asynUser *pasynUser, const char *value,
        size_t nChars, size_t *nActual)
{
    int function = pasynUser->reason;
    asynStatus status = asynSuccess;
    const char *functionName = "writeOctet";

    return asynSuccess;

    /* Set the parameter in the parameter library. */
    status = (asynStatus) setStringParam(function, (char *) value);

    if (function == NDFilePath)
    {
// not required for medipix
    }
    else
    {
        /* If this parameter belongs to a base class call its method */
        if (function < FIRST_medipix_PARAM)
            status = ADDriver::writeOctet(pasynUser, value, nChars, nActual);
    }

    /* Do callbacks so higher layers see any changes */
    status = (asynStatus) callParamCallbacks();

    if (status)
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                "%s:%s: status=%d, function=%d, value=%s", driverName,
                functionName, status, function, value);
    else
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER,
                "%s:%s: function=%d, value=%s\n", driverName, functionName,
                function, value);
    *nActual = nChars;
    return status;
}

/** Report status of the driver.
 * Prints details about the driver if details>0.
 * It then calls the ADDriver::report() method.
 * \param[in] fp File pointed passed by caller where the output is written to.
 * \param[in] details If >0 then driver details are printed.
 */
void medipixDetector::report(FILE *fp, int details)
{

    fprintf(fp, "medipix detector %s\n", this->portName);
    if (details > 0)
    {
        int nx, ny, dataType;
        getIntegerParam(ADSizeX, &nx);
        getIntegerParam(ADSizeY, &ny);
        getIntegerParam(NDDataType, &dataType);
        fprintf(fp, "  NX, NY:            %d  %d\n", nx, ny);
        fprintf(fp, "  Data type:         %d\n", dataType);
    }
    /* Invoke the base class method */
    ADDriver::report(fp, details);
}

extern "C" int medipixDetectorConfig(const char *portName,
        const char *LabviewCommandPort, const char *LabviewDataPort,
        int maxSizeX, int maxSizeY, int maxBuffers, int detectorType,
        size_t maxMemory, int priority, int stackSize)
{
    new medipixDetector(portName, LabviewCommandPort, LabviewDataPort, maxSizeX,
            maxSizeY, detectorType, maxBuffers, maxMemory, priority, stackSize);
    return (asynSuccess);
}

/** Constructor for medipix driver; most parameters are simply passed to ADDriver::ADDriver.
 * After calling the base class constructor this method creates a thread to collect the detector data,
 * and sets reasonable default values for the parameters defined in this class, asynNDArrayDriver, and ADDriver.
 * \param[in] portName The name of the asyn port driver to be created.
 * \param[in] LabviewPort The name of the asyn port previously created with drvAsynIPPortConfigure to
 *            communicate with Labview.
 * \param[in] maxSizeX The size of the medipix detector in the X direction.
 * \param[in] maxSizeY The size of the medipix detector in the Y direction.
 * \param[in] portName The name of the asyn port driver to be created.
 * \param[in] maxBuffers The maximum number of NDArray buffers that the NDArrayPool for this driver is
 *            allowed to allocate. Set this to -1 to allow an unlimited number of buffers.
 * \param[in] maxMemory The maximum amount of memory that the NDArrayPool for this driver is
 *            allowed to allocate. Set this to -1 to allow an unlimited amount of memory.
 * \param[in] priority The thread priority for the asyn port driver thread if ASYN_CANBLOCK is set in asynFlags.
 * \param[in] stackSize The stack size for the asyn port driver thread if ASYN_CANBLOCK is set in asynFlags.
 */
medipixDetector::medipixDetector(const char *portName,
        const char *LabviewCommandPort, const char *LabviewDataPort,
        int maxSizeX, int maxSizeY, int detectorType, int maxBuffers, size_t maxMemory,
        int priority, int stackSize)

:
        ADDriver(portName, 1, NUM_medipix_PARAMS, maxBuffers, maxMemory,
                asynInt32ArrayMask | asynFloat64ArrayMask
                        | asynGenericPointerMask | asynInt16ArrayMask,
                asynInt32ArrayMask | asynFloat64ArrayMask
                        | asynGenericPointerMask | asynInt16ArrayMask,
                ASYN_CANBLOCK, 1, /* ASYN_CANBLOCK=1, ASYN_MULTIDEVICE=0, autoConnect=1 */
                priority, stackSize),
        imagesRemaining(0)

{
    int status = asynSuccess;
    const char *functionName = "medipixDetector";
    size_t dims[2];

    startingUp = 1;
    strcpy(LabviewCommandPortName, LabviewCommandPort);
    strcpy(LabviewDataPortName, LabviewDataPort);

    detType = (medipixDetectorType) detectorType;

    /* Allocate the raw buffer we use to read image files.  Only do this once */
    dims[0] = maxSizeX;
    dims[1] = maxSizeY;
    /* Allocate the raw buffer we use for flat fields. */
    this->pFlatField = this->pNDArrayPool->alloc(2, dims, NDUInt32, 0, NULL);

// medipix is upside down by area detector standards
// this does not work - I need to invert using my own memory copy function
    // this->ADReverseY = 1;

    /* Connect to Labview */
    status = pasynOctetSyncIO->connect(LabviewCommandPort, 0,
            &this->pasynLabViewCmd, NULL);
    status = pasynOctetSyncIO->connect(LabviewDataPort, 0,
            &this->pasynLabViewData, NULL);

    cmdConnection = new mpxConnection(pasynUserSelf, pasynLabViewCmd, this);
    dataConnection = new mpxConnection(pasynUserSelf, pasynLabViewData, this);

    cmdConnection->mpxCommand(MPXCMD_STOPACQUISITION, Labview_DEFAULT_TIMEOUT);

    createParam(medipixDelayTimeString, asynParamFloat64, &medipixDelayTime);
    createParam(medipixThreshold0String, asynParamFloat64, &medipixThreshold0);
    createParam(medipixThreshold1String, asynParamFloat64, &medipixThreshold1);
    createParam(medipixOperatingEnergyString, asynParamFloat64,
            &medipixOperatingEnergy);
    createParam(medipixThresholdApplyString, asynParamInt32,
            &medipixThresholdApply);
    createParam(medipixThresholdAutoApplyString, asynParamInt32,
            &medipixThresholdAutoApply);
    createParam(medipixArmedString, asynParamInt32, &medipixArmed);

    createParam(medipixmedpixThresholdScanString, asynParamInt32,
            &medipixThresholdScan);
    createParam(medipixStartThresholdScanString, asynParamFloat64,
            &medipixStartThresholdScan);
    createParam(medipixStopThresholdScanString, asynParamFloat64,
            &medipixStopThresholdScan);
    createParam(medipixStepThresholdScanString, asynParamFloat64,
            &medipixStepThresholdScan);
    createParam(medipixStartThresholdScanningString, asynParamInt32,
            &medipixStartThresholdScanning);
    createParam(medipixCounterDepthString, asynParamInt32,
            &medipixCounterDepth);
    createParam(medipixResetString, asynParamInt32, &medipixReset);
    createParam(medipixSoftwareTriggerString, asynParamInt32,
            &medipixSoftwareTrigger);

    createParam(medipixEnableCounter1String, asynParamInt32,
            &medipixEnableCounter1);
    createParam(medipixContinuousRWString, asynParamInt32,
            &medipixContinuousRW);

// XBPM Specific parameters
    createParam(medipixProfileControlString, asynParamInt32,
            &medipixProfileControl);
    int res = createParam(medipixProfileXString, asynParamInt32Array,
            &medipixProfileX);
    res = createParam(medipixProfileYString, asynParamInt32Array,
            &medipixProfileY);

    /* Set some default values for parameters */
    status = setStringParam(ADManufacturer, "Medipix Consortium");
    status |= setStringParam(ADModel, "medipix");
    status |= setIntegerParam(ADMaxSizeX, maxSizeX);
    status |= setIntegerParam(ADMaxSizeY, maxSizeY);
    status |= setIntegerParam(ADSizeX, maxSizeX);
    status |= setIntegerParam(ADSizeX, maxSizeX);
    status |= setIntegerParam(ADSizeY, maxSizeY);
    status |= setIntegerParam(NDArraySizeX, maxSizeX);
    status |= setIntegerParam(NDArraySizeY, maxSizeY);
    status |= setIntegerParam(NDArraySize, 0);
    status |= setIntegerParam(NDDataType, NDUInt32);
    status |= setIntegerParam(ADImageMode, ADImageContinuous);
    status |= setIntegerParam(ADTriggerMode, TMInternal);
    status |= setIntegerParam(medipixProfileControl, MPXPROFILES_IMAGE);

// attempt to clear the spurious error on startup (failed - not sure where this is coming from?)
//      Medipix1TestFileName devAsynOctet: writeIt requested 0 but sent 10780660 bytes
    status |= setStringParam(NDFileName, "image.bmp");

// allocate space for the waveforms
    this->profileX = (int*) malloc(maxSizeX * sizeof(int));
    this->profileY = (int*) malloc(maxSizeY * sizeof(int));

    if (status)
    {
        printf("%s: unable to set camera parameters\n", functionName);
        return;
    }

    /* Create the thread that updates the images */
    status = (epicsThreadCreate("medipixDetTask", epicsThreadPriorityMedium,
            epicsThreadGetStackSize(epicsThreadStackMedium),
            (EPICSTHREADFUNC) medipixTaskC, this) == NULL);
    if (status)
    {
        printf("%s:%s epicsThreadCreate failure for image task\n", driverName,
                functionName);
        return;
    }

    /* Create the thread that monitors detector status (temperature, humidity, etc). */
    status = (epicsThreadCreate("medipixStatusTask", epicsThreadPriorityMedium,
            epicsThreadGetStackSize(epicsThreadStackMedium),
            (EPICSTHREADFUNC) medipixStatusC, this) == NULL);
    if (status)
    {
        printf("%s:%s epicsThreadCreate failure for status task\n", driverName,
                functionName);
        return;
    }

}

/* Code for iocsh registration */
static const iocshArg medipixDetectorConfigArg0 =
{ "Port name", iocshArgString };
static const iocshArg medipixDetectorConfigArg1 =
{ "Labview cmd port", iocshArgString };
static const iocshArg medipixDetectorConfigArg2 =
{ "Labview data port", iocshArgString };
static const iocshArg medipixDetectorConfigArg3 =
{ "maxSizeX", iocshArgInt };
static const iocshArg medipixDetectorConfigArg4 =
{ "maxSizeY", iocshArgInt };
static const iocshArg medipixDetectorConfigArg5 =
{ "detectorType", iocshArgInt };
static const iocshArg medipixDetectorConfigArg6 =
{ "maxBuffers", iocshArgInt };
static const iocshArg medipixDetectorConfigArg7 =
{ "maxMemory", iocshArgInt };
static const iocshArg medipixDetectorConfigArg8 =
{ "priority", iocshArgInt };
static const iocshArg medipixDetectorConfigArg9 =
{ "stackSize", iocshArgInt };
static const iocshArg * const medipixDetectorConfigArgs[] =
{ &medipixDetectorConfigArg0, &medipixDetectorConfigArg1,
        &medipixDetectorConfigArg2, &medipixDetectorConfigArg3,
        &medipixDetectorConfigArg4, &medipixDetectorConfigArg5,
        &medipixDetectorConfigArg6, &medipixDetectorConfigArg7,
        &medipixDetectorConfigArg8, &medipixDetectorConfigArg9 };
static const iocshFuncDef configmedipixDetector =
{ "medipixDetectorConfig", 8, medipixDetectorConfigArgs };
static void configmedipixDetectorCallFunc(const iocshArgBuf *args)
{
    medipixDetectorConfig(args[0].sval, args[1].sval, args[2].sval,
            args[3].ival, args[4].ival, args[5].ival, args[6].ival,
            args[7].ival, args[8].ival, args[9].ival);
}

static void medipixDetectorRegister(void)
{

    iocshRegister(&configmedipixDetector, configmedipixDetectorCallFunc);
}

extern "C"
{
epicsExportRegistrar(medipixDetectorRegister);
}


/**
 * Interface library to medipix Labview system.
 * 
 * Matthew Pearson
 * Nov 2011
 */

#ifndef MPX_LOW_H
#define MPX_LOW_H

#include <asynOctetSyncIO.h>

extern int mpxSet(const char *command, const char *value);
extern int mpxGet(const char *command, char *value);
extern int mpxCmd(const char *command);
extern int mpxData(unsigned int *data);
extern int mpxConnect(const char *host, int commandPort, int dataPort);
extern int mpxIsConnected(int *conn);
extern int mpxDisconnect(void);



/* Fixed protocol names*/
#define MPX_HEADER "MPX"
#define MPX_SET "SET"
#define MPX_GET "GET"
#define MPX_CMD "CMD"
#define MPX_MSG_LEN_DIGITS 10
#define MPX_IMG_HDR_LEN 256

#define MPX_MAXLINE 256
#define MPX_DATAFRAME 128000

#define MPX_X_SIZE 256
#define MPX_Y_SIZE 256
#define MPX_IMAGE_PIXELS 65536
#define MPX_IMAGE_BYTES MPX_IMAGE_PIXELS * 2 // 16 bit pixels

// error definitions
#define MPX_OK 0    			/*Ok*/
#define MPX_ERR 1               /*Unknown Error*/
#define MPX_ERR_CMD 2   		/*Command not known.*/
#define MPX_ERR_PARAM 3 		/*Param out of range.*/
#define MPX_ERR_CONN 100  		/*Not connected to detector.*/
#define MPX_ERR_WRITE 101 		/*Error writing to socket.*/
#define MPX_ERR_READ 102 		/*Error reading from socket.*/
#define MPX_ERR_LEN 103 		/*Length of command and value too long, or NULL*/
#define MPX_ERR_DATA 110 		/*Data formatting error.*/
#define MPX_ERR_UNEXPECTED 111 	/*unexpected response from labview */

// variables
#define MPX_GETSOFTWAREVERSION "GETSOFTWAREVERSION"
#define MPX_DETECTORSTATUS "DETECTORSTATUS"
// commands
#define MPX_STARTACQUISITION "STARTACQUISITION"

#endif /* MPX_LOW_H */

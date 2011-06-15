#ifndef Definitions_H
#define Definitions_H

//CConfigReaderLayerType
#define CConfigReaderLayerTypeDataBase    1
#define CConfigReaderLayerTypeStyled      2
#define CConfigReaderLayerTypeCascaded    3

//CRequest
#define SERVICE_WMS                       1
#define SERVICE_WCS                       2
#define REQUEST_WMS_GETCAPABILITIES       1
#define REQUEST_WMS_GETMAP                2
#define REQUEST_WMS_GETLEGENDGRAPHIC      3
#define REQUEST_WMS_GETFEATUREINFO        4
#define REQUEST_WCS_GETCAPABILITIES       5
#define REQUEST_WCS_DESCRIBECOVERAGE      6
#define REQUEST_WCS_GETCOVERAGE           7
#define REQUEST_WMS_GETMETADATA           8
#define REQUEST_WMS_GETSTYLES             9
#define REQUEST_UPDATEDB                  100

//Legend
#define LEGEND_WIDTH                      175
#define LEGEND_HEIGHT                     200

//WMS versions
#define WMS_VERSION_1_0_0                 1
#define WMS_VERSION_1_1_1                 2

//Exceptions
#define EXCEPTIONS_PLAINTEXT              0
#define WMS_EXCEPTIONS_IMAGE              1
#define WMS_EXCEPTIONS_BLANKIMAGE         2
#define WMS_EXCEPTIONS_XML_1_1_1          3
#define WCS_EXCEPTIONS_XML_1_0_0          4

//WCS Versions
#define WCS_VERSION_1_0                   1

// XMLGen templates
#define WMS_1_0_0_HEADERFILE              "./XMLTemplates/WMS_1.0.0_GetCapabilities_Header.dat"
#define WMS_1_0_0_LAYERHEADERFILE         "./XMLTemplates/WMS_1.0.0_GetCapabilities_LayerHeader.dat"

#define WMS_1_1_1_HEADERFILE              "./XMLTemplates/WMS_1.1.1_GetCapabilities_Header.dat"
#define WMS_1_1_1_LAYERHEADERFILE         "./XMLTemplates/WMS_1.1.1_GetCapabilities_LayerHeader.dat"

#define WCS_1_0_HEADERFILE                "./XMLTemplates/WCS_1.0.0_GetCapabilities_Header.dat"

//CNetCDFReader opening options
#define CNETCDFREADER_MODE_OPEN_HEADER    1
#define CNETCDFREADER_MODE_OPEN_ALL       2
#define CNETCDFREADER_MODE_GET_METADATA   3

//Log a summary of different time durations during execution
//#define MEASURETIME

//Web Coverage restriction and Get Feature Info restriction
#define ALLOW_NONE     1
#define ALLOW_WCS      2 
#define ALLOW_GFI      4
#define ALLOW_METADATA 8


#define ENABLE_CURL

#define showqueryinfo false
#endif

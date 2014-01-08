/******************************************************************************
 * 
 * Project:  ADAGUC Server
 * Purpose:  ADAGUC OGC Server
 * Author:   Maarten Plieger, plieger "at" knmi.nl
 * Date:     2013-06-01
 *
 ******************************************************************************
 *
 * Copyright 2013, Royal Netherlands Meteorological Institute (KNMI)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *      http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * 
 ******************************************************************************/

#include "CDataReader.h"
#include <math.h>
#include <float.h>
#include "CConvertASCAT.h"
#include "CConvertADAGUCVector.h"
#include "CConvertADAGUCPoint.h"
#include "CConvertCurvilinear.h"

const char *CDataReader::className="CDataReader";

//#define CDATAREADER_DEBUG
//#define MEASURETIME

#define uchar unsigned char
#define MAX_STR_LEN 8191



void writeLogFile2(const char * msg){
  char * logfile=getenv("ADAGUC_LOGFILE");
  if(logfile!=NULL){
    FILE * pFile = NULL;
    pFile = fopen (logfile , "a" );
    if(pFile != NULL){
      fputs  (msg, pFile );
      if(strncmp(msg,"[D:",3)==0||strncmp(msg,"[W:",3)==0||strncmp(msg,"[E:",3)==0){
        time_t myTime = time(NULL);
        tm *myUsableTime = localtime(&myTime);
        char szTemp[128];
        snprintf(szTemp,127,"%.4d-%.2d-%.2dT%.2d:%.2d:%.2dZ ",
                 myUsableTime->tm_year+1900,myUsableTime->tm_mon+1,myUsableTime->tm_mday,
                 myUsableTime->tm_hour,myUsableTime->tm_min,myUsableTime->tm_sec
                );
        fputs  (szTemp, pFile );
      }
      fclose (pFile);
    }//else CDBError("Unable to write logfile %s",logfile);
  }
}

/*void printStatus(const char *status,const char *a,...){
  va_list ap;
  char message[8192+1];
  va_start (ap, a);
  vsnprintf (message, 8192, a, ap);
  va_end (ap);
  message[8192]='\0';

  size_t s=strlen(message);
  size_t statuslen=strlen(status);
  char outMessage[80];
  char newStatus[statuslen+4];
  snprintf(newStatus,statuslen+3,"[%s]",status);
  statuslen=strlen(newStatus);
  int m=s;if(m>79)m=79;
  strncpy(outMessage,message,m);
  for(int j=m;j<80;j++)outMessage[j]=' ';
  
  strncpy(outMessage+79-statuslen,newStatus,statuslen);
  outMessage[79]='\0';
  printf("%s\n",outMessage);
}*/






int CDataReader::getCacheFileName(CDataSource *dataSource,CT::string *cacheFilename){
  
  if(dataSource==NULL)return 1;
  if(dataSource->srvParams==NULL)return 1;
#ifdef CDATAREADER_DEBUG    
  CDBDebug("GetCacheFileName");
#endif
  
  CT::string cacheLocation;dataSource->srvParams->getCacheDirectory(&cacheLocation);
  if(cacheLocation.empty())return 1;else if(cacheLocation.length()==0)return 1;  
#ifdef CDATAREADER_DEBUG  
  CDBDebug("/GetCacheFileName");
#endif  
  cacheFilename->copy(cacheLocation.c_str());
  cacheFilename->concat("/");
  
  
  
  CDirReader::makePublicDirectory(cacheFilename->c_str());
  
  
  if(dataSource->getFileName()==NULL){
     CDBError("No filename for datasource");
    return 1;
  }
  
  //Make the cache unique directory name, based on the filename
  CT::string validFileName(dataSource->getFileName());
  //Replace : and / by nothing, so we can use the string as a directory name
  validFileName.replaceSelf(":",""); 
  validFileName.replaceSelf("/",""); 
  //Concat the filename to the cache directory
  cacheFilename->concat(&validFileName);
  cacheFilename->concat("cache");

  CDirReader::makePublicDirectory(cacheFilename->c_str());
  
  //Now make the filename, based on variable name and dimension properties
  int timeStep = dataSource->getCurrentTimeStep();
    
  cacheFilename->concat("/");
  cacheFilename->concat(dataSource->dataObject[0]->variableName.c_str());
#ifdef CDATAREADER_DEBUG    
  CDBDebug("Add dimension properties to the filename");
#endif  
  //Add dimension properties to the filename
  if(dataSource->timeSteps[timeStep]->dims.dimensions.size()>0){
    for(size_t j=0;j<dataSource->timeSteps[timeStep]->dims.dimensions.size();j++){
      cacheFilename->printconcat("_[%s=%d]", 
                                      dataSource->timeSteps[timeStep]->dims.dimensions[j]->name.c_str(),
                                      dataSource->timeSteps[timeStep]->dims.dimensions[j]->index);
    }
  }
  
  
  return 0;
}

/**
 * Function to apply quickly scale and offset to a float data array
 * Is static so it can be used in a multithreaded way.
 */

/*class ApplyScaleAndOffsetFloatSettings{
  public:
    size_t start;size_t stop;float *data;float scale;float offset;
};

void *applyScaleAndOffsetFloatThread(void *vsettings){

  ApplyScaleAndOffsetFloatSettings *settings=(ApplyScaleAndOffsetFloatSettings *)vsettings;
  size_t start = settings->start;
  size_t stop = settings->stop;
  float *data = settings->data;
  float scale = settings ->scale;
  float offset = settings->offset;
  char msg[256];
  sprintf(msg,"sf %d - %d == %d\n",start,stop,stop-start);
  writeLogFile2(msg);
          
  for(size_t j=start;j<stop;j++){
    data[j]=data[j]*scale+offset;
  }
  return NULL;
}

int applyScaleAndOffsetFloat(size_t start,size_t stop,float *data,float scale,float offset){
  size_t numThreads=2;
  int errcode;
  int errorOccured = 0;
  int blocksDone = 0;
  int blockSize = (stop-start)/numThreads;
  if(blockSize <=0 )return 1;
  
  pthread_t threads[numThreads];
  ApplyScaleAndOffsetFloatSettings settings[numThreads];
  try{
    for(int j=0;j<numThreads;j++){
      settings[j].start=start+blocksDone;
      settings[j].stop=start+blocksDone+blockSize;
      blocksDone+=blockSize;
      if(j==numThreads-1)settings[j].stop=stop;
      settings[j].data=data;
      settings[j].scale=scale;
      settings[j].offset=offset;
      errcode=pthread_create(&threads[j],NULL,applyScaleAndOffsetFloatThread,(void*)(&settings[j]));
      if(errcode){throw(__LINE__);}
    }
  }catch(int line){
    errorOccured=1;
  }
  for (size_t j=0; j<numThreads; j++) {
    errcode=pthread_join(threads[j],NULL);
    if(errcode){ return 1;}
  }
  return errorOccured;
}*/

class Proc{
  public:
    DEF_ERRORFUNCTION();
  template <class T>
  static void swapPixelsAtLocation(CDataSource *dataSource,T*data){
    
   //T temp1,temp2;
   CDBDebug("Applying LON warp to -180 till 180 on the original data");
   
   int origWidth = dataSource->dWidth;
   dataSource->dWidth=int(360.0/dataSource->dfCellSizeX);
   
   double origBBOXLeft = ((double*)dataSource->varX->data)[0]-dataSource->dfCellSizeX/2;
   double origBBOXRight = ((double*)dataSource->varX->data)[dataSource->varX->getSize()-1]+dataSource->dfCellSizeX/2;
   double origBBOXWidth = (origBBOXRight - origBBOXLeft) ;
   
   //CDBDebug("BBOXW:(%f %f) origWidth: %d newWidth: %d cellSize: %f",origBBOXLeft,origBBOXLeft,origWidth,dataSource->dWidth,dataSource->dfCellSizeX);
   
   dataSource->dfBBOX[0] = origBBOXLeft;
   dataSource->dfBBOX[2] = origBBOXRight;
   CDBDebug("Old bbox = %f %f",dataSource->dfBBOX[0],dataSource->dfBBOX[2]);
   while( dataSource->dfBBOX[0]>-180){dataSource->dfBBOX[0]-=dataSource->dfCellSizeX;}
   while(dataSource->dfBBOX[0]<-180){ dataSource->dfBBOX[0]+=dataSource->dfCellSizeX;}
   dataSource->dfBBOX[2]=dataSource->dfBBOX[0]+360;
   
   
   /*int discrete = -180/dataSource->dfCellSizeX;
   discrete*=dataSource->dfCellSizeX;
   dataSource->dfBBOX[0]=discrete;
    discrete = 180/dataSource->dfCellSizeX;
   discrete*=dataSource->dfCellSizeX;
   dataSource->dfBBOX[2]=discrete;*/
   
   
   
   
   double nodataValue = (T)dataSource->dataObject[0]->dfNodataValue;
   if(dataSource->dataObject[0]->hasNodataValue == false){
     dataSource->dataObject[0]->hasNodataValue = true;
     dataSource->dataObject[0]->dfNodataValue = INFINITY; 
     nodataValue=dataSource->dataObject[0]->dfNodataValue ;
   }
    
    
    //void **a = &dataSource->dataObject[0]->cdfVariable->data;
  //  dataSource->dWidth=720;
   // dataSource->dHeight=360;
    //((T)dataSource->dataObject[0]->cdfVariable)
    void *newData = NULL;
    size_t imageSize = dataSource->dWidth*dataSource->dHeight;
    if(imageSize==0)imageSize=1;
    CDF::allocateData(dataSource->dataObject[0]->cdfVariable->getType(),&newData,imageSize);
    for(size_t j=0;j<imageSize;j++){
      ((T*)newData)[j]=(T)nodataValue;
    }
    
    T*oldData = (T*)dataSource->dataObject[0]->cdfVariable->data;
    data = (T*)newData;
    for(int y=0;y<dataSource->dHeight;y++){
      for(int x=0;x<origWidth;x=x+1){
        double lonX=((double(x)/double(origWidth))*origBBOXWidth)+origBBOXLeft;
        while(lonX<-180)lonX+=360;
        while(lonX>=180)lonX-=360;
        
        int newXIndex = int(floor((((lonX-dataSource->dfBBOX[0])/360))*double(dataSource->dWidth)+0.5));
        T value = oldData[x+y*origWidth];
        if(newXIndex>=0&&newXIndex<dataSource->dWidth){
          data[newXIndex+y*dataSource->dWidth]=value;
        }
      }
    }
    
   CDF::freeData(&dataSource->dataObject[0]->cdfVariable->data);
   (dataSource->dataObject[0]->cdfVariable->data)=newData;
  }
};

const char *Proc::className="Proc";

int CDataReader::open(CDataSource *dataSource, int mode){
  return open(dataSource,mode,-1,-1);
}
int CDataReader::open(CDataSource *dataSource, int x,int y){
  return open(dataSource,CNETCDFREADER_MODE_OPEN_ALL,x,y);
}


int CDataReader::parseDimensions(CDataSource *dataSource,int mode,int x, int y,CCache *cache){
  
  

  
  /**************************************************************************************************/
  /*  LEVEL 2 ASCAT COMPAT MODE!*/
  /**************************************************************************************************/
  dataSource->level2CompatMode = false;
  if(!dataSource->level2CompatMode)if(CConvertASCAT::convertASCATData(dataSource,mode)==0)dataSource->level2CompatMode=true;
  if(!dataSource->level2CompatMode)if(CConvertADAGUCVector::convertADAGUCVectorData(dataSource,mode)==0)dataSource->level2CompatMode=true;
  if(!dataSource->level2CompatMode)if(CConvertADAGUCPoint::convertADAGUCPointData(dataSource,mode)==0)dataSource->level2CompatMode=true;
  if(!dataSource->level2CompatMode)if(CConvertCurvilinear::convertCurvilinearData(dataSource,mode)==0)dataSource->level2CompatMode=true;     
  if(dataSource->level2CompatMode){
   cache->removeClaimedCachefile();
  }
  int status = 0;

  CDF::Variable * dataSourceVar=dataSource->dataObject[0]->cdfVariable;
  CDFObject *cdfObject = dataSource->dataObject[0]->cdfObject;
 
  if(dataSource->cfgLayer->Dimension.size()==0){
    #ifdef CDATAREADER_DEBUG  
    CDBDebug("Auto configuring dims");
    #endif
    autoConfigureDimensions(dataSource);
  }

  bool singleCellMode = false;
  
  if(x!=-1&&y!=-1){
    singleCellMode = true;
  }
  
  //CDBDebug("singlecellmode: %d %d %d",x,y,singleCellMode);
  
  // It is possible to skip every N cell in x and y. When set to 1, all data is displayed.
  // When set to 2, every second datacell is displayed, etc...
  
 
  // Retrieve X, Y Dimensions and Width, Height
  dataSource->dNetCDFNumDims = dataSourceVar->dimensionlinks.size();
  
  if(dataSource->dNetCDFNumDims<2){
    CDBError("Variable %s has less than two dimensions", dataSourceVar->name.c_str());
    return 1;
  }
  
  #ifdef CDATAREADER_DEBUG  
  CDBDebug("Number of dimensions = %d",dataSource->dNetCDFNumDims);
  #endif
  
  
 
  dataSource->dimXIndex=dataSource->dNetCDFNumDims-1;
  dataSource->dimYIndex=dataSource->dNetCDFNumDims-2;
  
  dataSource->swapXYDimensions = false;

  //If our X dimension has a character y/lat in it, XY dims are probably swapped.
  CT::string dimensionXName=dataSourceVar->dimensionlinks[dataSource->dimXIndex]->name.c_str();
  
  dimensionXName.toLowerCaseSelf();
  if(dimensionXName.indexOf("y")!=-1||dimensionXName.indexOf("lat")!=-1)dataSource->swapXYDimensions=true;
  
  //dataSource->swapXYDimensions=true;
  if(dataSource->swapXYDimensions){
    dataSource->dimXIndex=dataSource->dNetCDFNumDims-2;
    dataSource->dimYIndex=dataSource->dNetCDFNumDims-1;
  }
  
  CDF::Dimension *dimX=dataSourceVar->dimensionlinks[dataSource->dimXIndex];
  CDF::Dimension *dimY=dataSourceVar->dimensionlinks[dataSource->dimYIndex];
  
  if(dimX==NULL||dimY==NULL){CDBError("X and or Y dims not found...");return 1;}
 

 #ifdef CDATAREADER_DEBUG  
 CDBDebug("Found xy dims for var %s:  %s and %s",dataSourceVar->name.c_str(),dimX->name.c_str(),dimY->name.c_str());
 #endif

 //Read X and Y dimension data completely.
 dataSource->varX=cdfObject->getVariableNE(dimX->name.c_str());
 dataSource->varY=cdfObject->getVariableNE(dimY->name.c_str());
 if(dataSource->varX==NULL||dataSource->varY==NULL){CDBError("X ('%s') and or Y ('%s') vars not found...",dimX->name.c_str(),dimY->name.c_str());return 1;}

  dataSource->stride2DMap=1;
  
  
  while(dimX->length/dataSource->stride2DMap>360*4){
    dataSource->stride2DMap++;
  }
  
  //When we are reading from cache, the file has been written based on strided data
  if(cache->cacheIsAvailable()){
    dataSource->stride2DMap=1;
  }
  dataSource->stride2DMap=1;
  
  if(dataSource->level2CompatMode){
    dataSource->stride2DMap=1;
  }
  
  dataSource->dWidth=dimX->length/dataSource->stride2DMap;
  dataSource->dHeight=dimY->length/dataSource->stride2DMap;
  
  if(singleCellMode){
    dataSource->dWidth=2;
    dataSource->dHeight=2;
  }
  
  size_t start[dataSource->dNetCDFNumDims+1];
  
  //Everything starts at zero
  for(int j=0;j<dataSource->dNetCDFNumDims;j++){start[j]=0;}
  
  
  
  //Set other dimensions than X and Y.
  if(dataSource->dNetCDFNumDims>2){
    for(int j=0;j<dataSource->dNetCDFNumDims-2;j++){
      start[j]=dataSource->getDimensionIndex(dataSourceVar->dimensionlinks[j]->name.c_str());//dOGCDimValues[0];// time dim
    }
  }

  if(dataSource->level2CompatMode == false){
      
    size_t sta[1],sto[1];ptrdiff_t str[1];
    sta[0]=start[dataSource->dimXIndex];str[0]=dataSource->stride2DMap; sto[0]=dataSource->dWidth;
    if(singleCellMode){sta[0]=0;str[0]=1;sto[0]=2;}
    //CDBDebug("[%d %d %d] for %s/%s",sta[0],str[0],sto[0],dataSourceVar->name.c_str(),dataSource->varX->name.c_str());
    status = dataSource->varX->readData(CDF_DOUBLE,sta,sto,str);
    if(status!=0){
      CDBError("Unable to read x dimension with name %s for variable %s",dataSource->varX->name.c_str(),dataSourceVar->name.c_str());
      return 1;
    }
    sta[0]=start[dataSource->dimYIndex];sto[0]=dataSource->dHeight;
    if(singleCellMode){
      sta[0]=0;str[0]=1;sto[0]=2;
      
    }
    status = dataSource->varY->readData(CDF_DOUBLE,sta,sto,str);if(status!=0){
      CDBError("Unable to read y dimension for variable %s",dataSourceVar->name.c_str());
      for(size_t j=0;j<dataSource->varY->dimensionlinks.size();j++){
            CDBDebug("For var %s, reading dim %s of size %d (%d %d %d)", dataSource->varY->name.c_str(),dataSource->varY->dimensionlinks[j]->name.c_str(),dataSource->varY->dimensionlinks[j]->getSize(),sta[j],sto[j],str[j]);
          }
      return 1;
    }
  }

  
  
  // Calculate cellsize based on read X,Y dims
  double *dfdim_X=(double*)dataSource->varX->data;
  double *dfdim_Y=(double*)dataSource->varY->data;
  
//   CDBDebug("dfdim_X :");
//   for(int j=0;j<dataSource->dWidth;j++){
//     CDBDebug("dfdim_X %d / %f",j,dfdim_X[j]);
//   }
  
  if(dfdim_X == NULL || dfdim_Y == NULL){
    CDBError("For variable '%s': No data available for '%s' or '%s' dimensions",dataSourceVar->name.c_str(),dataSource->varX->name.c_str(),dataSource->varY->name.c_str());
    return 1;
  }
  
  //CDBDebug("SOFAR %d %d %f %f",dataSource->dWidth,dataSource->dHeight,dfdim_X[0],dfdim_Y[0]);
  
  dataSource->dfCellSizeX=(dfdim_X[dataSource->dWidth-1]-dfdim_X[0])/double(dataSource->dWidth-1);
  dataSource->dfCellSizeY=(dfdim_Y[dataSource->dHeight-1]-dfdim_Y[0])/double(dataSource->dHeight-1);
  
  //CDBDebug("cX: %f W: %d BBOXL: %f BBOXR: %f",dataSource->dfCellSizeX,dataSource->dWidth,dfdim_X[0],dfdim_X[dataSource->dWidth-1]);
  // Calculate BBOX
  dataSource->dfBBOX[0]=dfdim_X[0]-dataSource->dfCellSizeX/2.0f;
  dataSource->dfBBOX[1]=dfdim_Y[dataSource->dHeight-1]+dataSource->dfCellSizeY/2.0f;
  dataSource->dfBBOX[2]=dfdim_X[dataSource->dWidth-1]+dataSource->dfCellSizeX/2.0f;
  dataSource->dfBBOX[3]=dfdim_Y[0]-dataSource->dfCellSizeY/2.0f;;
  

  if(dimensionXName.equals("col")){
    dataSource->dfBBOX[2]=dfdim_X[0]-dataSource->dfCellSizeX/2.0f;
    dataSource->dfBBOX[3]=dfdim_Y[dataSource->dHeight-1]+dataSource->dfCellSizeY/2.0f;
    dataSource->dfBBOX[0]=dfdim_X[dataSource->dWidth-1]+dataSource->dfCellSizeX/2.0f;
    dataSource->dfBBOX[1]=dfdim_Y[0]-dataSource->dfCellSizeY/2.0f;;
  }
  
#ifdef MEASURETIME
  StopWatch_Stop("XY dimensions read");
#endif

  // Retrieve CRS
  
  //Check if projection is overidden in the config file
  if(dataSource->cfgLayer->Projection.size()==1){
    //Read projection information from configuration
    if(dataSource->cfgLayer->Projection[0]->attr.id.empty()==false){
      dataSource->nativeEPSG.copy(dataSource->cfgLayer->Projection[0]->attr.id.c_str());
    }else{
      dataSource->nativeEPSG.copy("EPSG:4326");
      //dataSource->nativeEPSG.copy("unknown");
    }
    //Read proj4 string
    if(dataSource->cfgLayer->Projection[0]->attr.proj4.empty()==false){
      dataSource->nativeProj4.copy(dataSource->cfgLayer->Projection[0]->attr.proj4.c_str());
    }
    else {
      dataSource->nativeProj4.copy("+proj=longlat +ellps=WGS84 +datum=WGS84 +no_defs");
      //dataSource->nativeProj4.copy("unknown");
    }
  }else{
    // If undefined, set standard lat lon projection
    dataSource->nativeProj4.copy("+proj=longlat +ellps=WGS84 +datum=WGS84 +no_defs");
    //dataSource->nativeEPSG.copy("EPSG:4326");
    //dataSource->nativeProj4.copy("unknown");
    dataSource->nativeEPSG.copy("EPSG:4326");
    //Read projection attributes from the file
    CDF::Attribute *projvarnameAttr = dataSourceVar->getAttributeNE("grid_mapping");
    if(projvarnameAttr!=NULL){
      CDF::Variable * projVar = cdfObject->getVariableNE(projvarnameAttr->toString().c_str());
      if(projVar==NULL){CDBWarning("projection variable '%s' not found",(char*)projvarnameAttr->data);}
      else {
        //Get proj4_params according to ADAGUC metadata
        CDF::Attribute *proj4Attr = projVar->getAttributeNE("proj4_params");
        
        //If not found try alternative one
        if (proj4Attr==NULL) proj4Attr = projVar->getAttributeNE("proj4");
        
        //If a proj4 string was found set it in the datasource.
        if(proj4Attr!=NULL)dataSource->nativeProj4.copy(proj4Attr->toString().c_str());
        
        //if still not found, try to compose a proj4 string based on Climate and Forecast Conventions
        if (proj4Attr==NULL){
          CProj4ToCF proj4ToCF;
          proj4ToCF.debug=true;
          CT::string projString;
          status = proj4ToCF.convertCFToProj(projVar,&projString);
          if(status==0){
            //Projection string was created, set it in the datasource.
            dataSource->nativeProj4.copy(projString.c_str());
            CDBDebug("Autogen proj4 string: %s",projString.c_str());
          }
        }
        
        

        //Get EPSG_code
        CDF::Attribute *epsgAttr = projVar->getAttributeNE("EPSG_code");
        if(epsgAttr!=NULL){dataSource->nativeEPSG.copy((char*)epsgAttr->data);}else
        {
          //Make a projection code based on PROJ4: namespace
          dataSource->nativeEPSG.print("PROJ4:%s",dataSource->nativeProj4.c_str());
          dataSource->nativeEPSG.encodeURLSelf();
        }
        //else {CDBWarning("EPSG_code not found in variable %s",(char*)projvarnameAttr->data);}
      }
    }
  }
  #ifdef CDATAREADER_DEBUG
  CDBDebug("PROJ4 = [%s]",dataSource->nativeProj4.c_str());
  #endif
  
  // Lon transformation is used to swap datasets from 0-360 degrees to -180 till 180 degrees
  //Swap data from >180 degrees to domain of -180 till 180 in case of lat lon source data
   dataSource->useLonTransformation = -1;
  
  if(singleCellMode == true){

    return 0;
  }
  if(dataSource->srvParams->requestType==REQUEST_WMS_GETFEATUREINFO||dataSource->srvParams->requestType==REQUEST_WMS_GETPOINTVALUE){
  
    return 0;
  }
  
  
  
  if(dataSource->level2CompatMode==false && dataSource->dfCellSizeX > 0){
  
    
    if( dataSource->srvParams->isLonLatProjection(&dataSource->nativeProj4)){
      size_t j=0;
      
      
      
      for(j=0;j<dataSource->varX->getSize();j++){
        //CDBDebug("%d == %f",j,((double*)dataSource->varX->data)[j]);
        if(((double*)dataSource->varX->data)[j]>=180.0)break;
        if(((double*)dataSource->varX->data)[j]<=-180.0)break;
      }
      
      
      if(j!=dataSource->varX->getSize()){
        CDBDebug("OK %f",dataSource->dfCellSizeX);
        dataSource->useLonTransformation=j;
        //dataSource->dfBBOX[0]=-180;
        //dataSource->dfBBOX[2]=180;
        while( dataSource->dfBBOX[0]>-180){dataSource->dfBBOX[0]-=dataSource->dfCellSizeX;}
        while(dataSource->dfBBOX[0]<-180){ dataSource->dfBBOX[0]+=dataSource->dfCellSizeX;}
        dataSource->dfBBOX[2]=dataSource->dfBBOX[0]+360;
        
        //When cache is available, the 2D field is already stored as a lontransformed field. We should not do this again.
        //The lat/lons are always kept original, so they needed to be shifted (done above)
        
        if(cache->cacheIsAvailable()){
          dataSource->useLonTransformation = -1;
        }
      }
      
    }
  }
  
  return 0;
}




int CDataReader::open(CDataSource *dataSource,int mode,int x,int y){
  //Perform some checks on pointers
  if(dataSource==NULL){CDBError("Invalid dataSource");return 1;}
  if(dataSource->getFileName()==NULL){CDBError("Invalid NetCDF filename (NULL)");return 1;}
  
  #ifdef CDATAREADER_DEBUG
    CDBDebug("Open mode:%d x:%d y:%d",mode,x,y);
#endif
  
  
  
  bool singleCellMode = false;
  
  if(x!=-1&&y!=-1){
    singleCellMode = true;
  }
  
  CT::string dataSourceFilename;
  dataSourceFilename.copy(dataSource->getFileName());
 
  CCache cache;
  CT::string cacheFilename;

  //Use autoscale of legendcolors when the legendscale factor has been set to zero.
  if(dataSource->stretchMinMaxDone == false){
    if(dataSource->legendScale==0.0f)dataSource->stretchMinMax=true;else dataSource->stretchMinMax=false;
  }
 
  
  
  bool enableDataCache=false;
  if(dataSource->cfgLayer->Cache.size()>0){
    if(dataSource->cfgLayer->Cache[0]->attr.enabled.equals("true")){
      
      enableDataCache=true;
       //Get cachefilename
      if(getCacheFileName(dataSource,&cacheFilename)!=0){
        enableDataCache=false;
      }
    }
  }
  
  //Check wether we should use cache or not (in case of OpenDAP, this speeds up things a lot)
  if(enableDataCache){
    cache.checkCacheSystemReady(cacheFilename.c_str());
  }
  
  if(mode!=CNETCDFREADER_MODE_OPEN_ALL){
    enableDataCache=false;
  }
  if(singleCellMode){
    enableDataCache=false;
  }
   
  CDFObject *cdfObject = NULL;
  
  
  if(mode == CNETCDFREADER_MODE_OPEN_DIMENSIONS  || mode == CNETCDFREADER_MODE_OPEN_HEADER ){
    cdfObject = CDFObjectStore::getCDFObjectStore()->getCDFObjectHeader(dataSource->srvParams,dataSourceFilename.c_str());
   
    enableDataCache = false;
  }
  
  if(mode == CNETCDFREADER_MODE_OPEN_ALL|| mode == CNETCDFREADER_MODE_GET_METADATA){
    if(cache.cacheIsAvailable()){
      //Remove DataReader configuration, because cache file is always netcdf.
      if(dataSource->cfgLayer->DataReader.size()>0){
        dataSource->cfgLayer->DataReader[0]->value.copy("NC");
      }
      
      //if(mode == CNETCDFREADER_MODE_OPEN_HEADER){
        //cdfObject = CDFObjectStore::getCDFObjectStore()->getCDFObjectHeader(dataSource->srvParams,dataSourceFilename.c_str());
      //}else 
      {
        //CDBDebug("Opening cachefile %s with mode %d",cacheFilename.c_str(),mode);
        cdfObject = CDFObjectStore::getCDFObjectStore()->getCDFObject(dataSource,cacheFilename.c_str());
        
        if(mode == CNETCDFREADER_MODE_OPEN_ALL){
          //In case of cache, just one timestep is stored
          int timeStep = dataSource->getCurrentTimeStep();
          for(size_t j=0;j<dataSource->timeSteps[timeStep]->dims.dimensions.size();j++){
            dataSource->timeSteps[timeStep]->dims.dimensions[j]->index=0;
          }
        }
      }
    }else{
      
      //We just open the file in the standard way, without cache
      //CDBDebug("Opening realfile %s with mode %d",dataSourceFilename.c_str(),mode);
      cdfObject=CDFObjectStore::getCDFObjectStore()->getCDFObject(dataSource,dataSourceFilename.c_str());
    
    }
  }

  //Check whether we really have a cdfObject
  if(cdfObject==NULL){CDBError("Unable to get CDFObject from store");return 1;}
  

  if(dataSource->attachCDFObject(cdfObject)!=0){
    CDBError("Unable to attach CDFObject");
    return 1;
  }

  
  //Shorthand pointer
  CDF::Variable *var[dataSource->dataObject.size()+1];

  for(size_t varNr=0;varNr<dataSource->dataObject.size();varNr++){
    var[varNr] = dataSource->dataObject[varNr]->cdfVariable;//cdfObject->getVariableNE(dataSource->dataObject[varNr]->variableName.c_str());
    //Check if our variable has a statusflag
    std::vector<CDataSource::StatusFlag*> *statusFlagList=&dataSource->dataObject[0]->statusFlagList;
    CDataSource::readStatusFlags(var[varNr],statusFlagList);
    if(statusFlagList->size()>0){
      dataSource->dataObject[0]->hasStatusFlag=true;
    }
    dataSource->dataObject[varNr]->points.clear();
  }

  /* CT::string dumpString;
   CDF::dump(cdfObject,&dumpString);
  CDBDebug("\nSTART\n%s\nEND\n",dumpString.c_str());
  writeLogFile2(dumpString.c_str());*/
  
  
  
  if(parseDimensions(dataSource,mode,x,y,&cache)!=0){
    CDBError("Unable to parseDimensions");
    return 1;
  }
  
  
  
  if(dataSource->level2CompatMode){
   
    enableDataCache=false;
  }
  if(enableDataCache){
    if(cache.saveCacheFile()){
      if(cache.claimCacheFile()!=0){
        enableDataCache = false;
      }
    }
  }
  if(mode == CNETCDFREADER_MODE_OPEN_DIMENSIONS){
#ifdef CDATAREADER_DEBUG
    CDBDebug("Dimensions parsed");
#endif
    return 0;
  }
  

  
  
  
  size_t start[dataSource->dNetCDFNumDims+1];
  size_t count[dataSource->dNetCDFNumDims+1];
  ptrdiff_t stride[dataSource->dNetCDFNumDims+1];
  
  //Set X and Y dimensions start, count and stride
  for(int j=0;j<dataSource->dNetCDFNumDims;j++){start[j]=0; count[j]=1;stride[j]=1;}
  count[dataSource->dimXIndex]=dataSource->dWidth;
  count[dataSource->dimYIndex]=dataSource->dHeight;
  stride[dataSource->dimXIndex]=dataSource->stride2DMap;
  stride[dataSource->dimYIndex]=dataSource->stride2DMap;
  
  
  
  //Set other dimensions than X and Y.
  if(dataSource->dNetCDFNumDims>2){
    for(int j=0;j<dataSource->dNetCDFNumDims-2;j++){
      start[j]=dataSource->getDimensionIndex(var[0]->dimensionlinks[j]->name.c_str());//dOGCDimValues[0];// time dim
      //CDBDebug("%s==%d",dataSourceVar->dimensionlinks[j]->name.c_str(),start[j]);
    }
  }
  
  if(singleCellMode){
    dataSource->dWidth=2;
    dataSource->dHeight=2;
    start[dataSource->dimXIndex]=x;
    start[dataSource->dimYIndex]=y;
    count[dataSource->dimXIndex]=1;
    count[dataSource->dimYIndex]=1;
    stride[dataSource->dimXIndex]=1;
    stride[dataSource->dimYIndex]=1;
    
    //CDBDebug("%d %d",x,y);
  }
 

  
  
  //Determine dataSource->dataObject[0]->dataType of the variable we are going to read
  for(size_t varNr=0;varNr<dataSource->dataObject.size();varNr++){
    
    #ifdef CDATAREADER_DEBUG
    CDBDebug("Working on variable %s, %d/%d",dataSource->dataObject[varNr]->cdfVariable->name.c_str(),varNr,dataSource->dataObject.size());
    #endif
    //dataSource->dataObject[varNr]->dataType=var[varNr]->getType();
    /*if(var[varNr]->getType()==CDF_CHAR||var[varNr]->getType()==CDF_BYTE)dataSource->dataObject[varNr]->dataType=CDF_CHAR;
    if(var[varNr]->getType()==CDF_UBYTE)dataSource->dataObject[varNr]->dataType=CDF_UBYTE;
    
    if(var[varNr]->getType()==CDF_SHORT||var[varNr]->getType()==CDF_USHORT)dataSource->dataObject[varNr]->dataType=CDF_SHORT;
    if(var[varNr]->getType()==CDF_INT||var[varNr]->getType()==CDF_UINT)dataSource->dataObject[varNr]->dataType=CDF_INT;
    if(var[varNr]->getType()==CDF_FLOAT)dataSource->dataObject[varNr]->dataType=CDF_FLOAT;
    if(var[varNr]->getType()==CDF_DOUBLE)dataSource->dataObject[varNr]->dataType=CDF_DOUBLE;
    if(dataSource->dataObject[varNr]->dataType==CDF_NONE){
      CDBError("Invalid dataSource->dataObject[varNr]->dataType");
      return 1;
    }*/
    //Get Unit
    CDF::Attribute *varUnits=var[varNr]->getAttributeNE("units");
    if(varUnits!=NULL){
      dataSource->dataObject[varNr]->units.copy((char*)varUnits->data,varUnits->length);
    }else dataSource->dataObject[varNr]->units.copy("");
  
    // Check for packed data / hasScaleOffset
  
    CDF::Attribute *scale_factor = var[varNr]->getAttributeNE("scale_factor");
    CDF::Attribute *add_offset = var[varNr]->getAttributeNE("add_offset");
    if(scale_factor!=NULL){
      //Scale and offset will be applied further downwards in this function.
      dataSource->dataObject[varNr]->hasScaleOffset=true;
      // Currently two unpacked data types are supported (CF-1.4): float and double
      
      dataSource->dataObject[varNr]->cdfVariable->setType(CDF_FLOAT);
      
      if(scale_factor->getType()==CDF_FLOAT){
        //dataSource->dataObject[varNr]->dataType=CDF_FLOAT;
        dataSource->dataObject[varNr]->cdfVariable->setType(CDF_FLOAT);
      }
      if(scale_factor->getType()==CDF_DOUBLE){
        dataSource->dataObject[varNr]->cdfVariable->setType(CDF_DOUBLE);
      }
    
      //char dataTypeName[256];
      //CDF::getCDFDataTypeName(dataTypeName,255, dataSource->dataObject[varNr]->dataType);
      //CDBDebug("Dataset datatype for reading = %s sizeof(short)=%d",dataTypeName,sizeof(short));
    
      //Internally we use always double for scale and offset parameters:
      scale_factor->getData(&dataSource->dataObject[varNr]->dfscale_factor,1);
      
      if(add_offset!=NULL){
        add_offset->getData(&dataSource->dataObject[varNr]->dfadd_offset,1);
      }else dataSource->dataObject[varNr]->dfadd_offset=0;
    }
    
    //DataPostProc: Here our datapostprocessor comes into action!
    for(size_t dpi=0;dpi<dataSource->cfgLayer->DataPostProc.size();dpi++){
      CServerConfig::XMLE_DataPostProc * proc = dataSource->cfgLayer->DataPostProc[dpi];
      //Algorithm ax+b:
      if(proc->attr.algorithm.equals("ax+b")){
        dataSource->dataObject[varNr]->hasScaleOffset=true;
        dataSource->dataObject[varNr]->cdfVariable->setType(CDF_DOUBLE);
      
        //Apply offset
        if(proc->attr.b.empty()==false){
          CT::string b;
          b.copy(proc->attr.b.c_str());
          if(add_offset==NULL){
            dataSource->dataObject[varNr]->dfadd_offset=b.toDouble();
          }else{
            dataSource->dataObject[varNr]->dfadd_offset+=b.toDouble();
          }
        }
        //Apply scale
        if(proc->attr.a.empty()==false){
          CT::string a;
          a.copy(proc->attr.a.c_str());
          if(scale_factor==NULL){
            dataSource->dataObject[varNr]->dfscale_factor=a.toDouble();
          }else{
            dataSource->dataObject[varNr]->dfscale_factor*=a.toDouble();
          }
        }
      }
      //Apply units:
      if(proc->attr.units.empty()==false){
        dataSource->dataObject[varNr]->units=proc->attr.units.c_str();
      }
    
    }
    
    //Important!: The CDF datamodel needs to now the new datatype as well, in order to do right lon warping.
    //var[varNr]->getType()=dataSource->dataObject[varNr]->dataType;
    
     #ifdef CDATAREADER_DEBUG
    CDBDebug("/Finished Working on variable %s",dataSource->dataObject[varNr]->cdfVariable->name.c_str());
    #endif
  }
  
  #ifdef CDATAREADER_DEBUG
    
  #endif


  if(mode==CNETCDFREADER_MODE_GET_METADATA){
    #ifdef CDATAREADER_DEBUG
    CDBDebug("Get metadata");
    #endif

   
    dataSource->nrMetadataItems=0;
    char szTemp[MAX_STR_LEN+1];
    // Find out how many attributes we have
    dataSource->nrMetadataItems=cdfObject->attributes.size();
    //CDBDebug("NC_GLOBAL: %d",dataSource->nrMetadataItems);
    for(size_t i=0;i<cdfObject->variables.size();i++){
      dataSource->nrMetadataItems+=cdfObject->variables[i]->attributes.size();
      //CDBDebug("%s: %d",cdfObject->variables[i]->name.c_str(),cdfObject->variables[i]->attributes.size());
    }
    //Allocate data for the attributes
    if(dataSource->metaData != NULL)delete[] dataSource->metaData;
    dataSource->metaData = new CT::string[dataSource->nrMetadataItems+1];
    // Loop through each variable
    int nrAttributes = 0;
    CT::string *variableName;
    CT::string AttrValue;
    std::vector<CDF::Attribute *> attributes;
    char szType[3];
    for(size_t i=0;i<cdfObject->variables.size()+1;i++){
      if(i==0){
        attributes=cdfObject->attributes;
        variableName=&cdfObject->name;
      }else {
        attributes=cdfObject->variables[i-1]->attributes;
        variableName=&cdfObject->variables[i-1]->name;
      }

      // Loop through each attribute
      szType[1]='\0';
      for(size_t j=0;j<attributes.size();j++){
        CDF::Attribute *attribute;
        size_t m;
        attribute=attributes[j];
        szType[1]='\0';
        AttrValue.copy("");
        switch (attribute->getType()){
          case CDF_CHAR:{
            szType[0]='C';//char
            AttrValue.copy((char*)attribute->data);
          }
          break;
          case CDF_SHORT:{
            szType[0]='S';//short
            for(m=0; m < attribute->length-1; m++) {
              snprintf( szTemp, MAX_STR_LEN,"%d, ",((short*)attribute->data)[m] );
              AttrValue.concat(szTemp);
            }
            snprintf( szTemp, MAX_STR_LEN,"%d",((short*)attribute->data)[m]);
            AttrValue.concat(szTemp);
          }
          break;
          case CDF_INT:{
            szType[0]='I';//Int
            for(m=0; m < attribute->length-1; m++) {
              snprintf( szTemp, MAX_STR_LEN,"%d, ",((int*)attribute->data)[m] );
              AttrValue.concat(szTemp);
            }
            snprintf( szTemp, MAX_STR_LEN,"%d",((int*)attribute->data)[m]);
            AttrValue.concat(szTemp);
          }
          break;
          case CDF_FLOAT:{
            szType[0]='F';//Float
            for(m=0; m < attribute->length-1; m++) {
              snprintf( szTemp, MAX_STR_LEN,"%f, ",((float*)attribute->data)[m] );
              AttrValue.concat(szTemp);
            }
            snprintf( szTemp, MAX_STR_LEN,"%f",((float*)attribute->data)[m]);
            AttrValue.concat(szTemp);
          }
          break;
          case CDF_DOUBLE:{
            szType[0]='D';//Double
            for(m=0; m < attribute->length-1; m++) {
              snprintf( szTemp, MAX_STR_LEN,"%f, ",((double*)attribute->data)[m] );
              AttrValue.concat(szTemp);
            }
            snprintf( szTemp, MAX_STR_LEN,"%f",((double*)attribute->data)[m]);
            AttrValue.concat(szTemp);
          }
          break;
        }
        snprintf(szTemp,MAX_STR_LEN,"%s#[%s]%s>%s",variableName->c_str(),szType,attribute->name.c_str(),AttrValue.c_str());
        //CDBDebug("variableName %d==%d %s",nrAttributes,dataSource->nrMetadataItems,szTemp);
        dataSource->metaData[nrAttributes++].copy(szTemp);
      }
    }
    #ifdef CDATAREADER_DEBUG
    CDBDebug("Metadata Finished.");
    #endif
    return 0;
  }

  #ifdef CDATAREADER_DEBUG
    
  #endif

  if(mode==CNETCDFREADER_MODE_OPEN_ALL){
      #ifdef CDATAREADER_DEBUG
    CDBDebug("CNETCDFREADER_MODE_OPEN_ALL");
  #endif

  #ifdef MEASURETIME
    StopWatch_Stop("start reading image data");
  #endif
    //for(size_t varNr=0;varNr<dataSource->dataObject.size();varNr++)
      
  
 
    for(size_t varNr=0;varNr<dataSource->dataObject.size();varNr++)    {
       // double dfNoData = 0;
      #ifdef MEASURETIME
      StopWatch_Stop("Reading _FillValue");
      #endif
      CDF::Attribute *fillValue = var[varNr]->getAttributeNE("_FillValue");
      if(fillValue!=NULL){
        
        fillValue->getData(&dataSource->dataObject[varNr]->dfNodataValue,1);
        dataSource->dataObject[varNr]->hasNodataValue = true;
      }else {
        dataSource->dataObject[varNr]->hasNodataValue=false;
      }
      
      
    
      
      //if( var[varNr]->data==NULL){
      if( dataSource->level2CompatMode == false){
        //#ifdef MEASURETIME
        //StopWatch_Stop("Freeing data");
        //#endif
        
        //Read variable data
        var[varNr]->freeData();

        #ifdef MEASURETIME
        StopWatch_Stop("start reading data");
        #endif
        
         
         
        if(var[varNr]->readData(var[varNr]->getType(),start,count,stride)!=0){
          CDBError("Unable to read data for variable %s in file %s",var[varNr]->name.c_str(),dataSource->getFileName());
          
          for(size_t j=0;j<var[varNr]->dimensionlinks.size();j++){
            CDBDebug("%s %d %d %d",var[varNr]->dimensionlinks[j]->name.c_str(),start[j],count[j],stride[j]);
          }
          
          return 1;
        }
        
        //Swap data from >180 degrees to domain of -180 till 180 in case of lat lon source data
        if(dataSource->useLonTransformation!=-1&&!cache.cacheIsAvailable()){
          //int splitPX=dataSource->useLonTransformation;
          switch (var[varNr]->getType()){
            case CDF_CHAR  : Proc::swapPixelsAtLocation(dataSource,(char*)var[varNr]->data);break;
            case CDF_BYTE  : Proc::swapPixelsAtLocation(dataSource,(char*)var[varNr]->data);break;
            case CDF_UBYTE : Proc::swapPixelsAtLocation(dataSource,(uchar*)var[varNr]->data);break;
            case CDF_SHORT : Proc::swapPixelsAtLocation(dataSource,(short*)var[varNr]->data);break;
            case CDF_USHORT: Proc::swapPixelsAtLocation(dataSource,(ushort*)var[varNr]->data);break;
            case CDF_INT   : Proc::swapPixelsAtLocation(dataSource,(int*)var[varNr]->data);break;
            case CDF_UINT  : Proc::swapPixelsAtLocation(dataSource,(unsigned int*)var[varNr]->data);break;
            case CDF_FLOAT : Proc::swapPixelsAtLocation(dataSource,(float*)var[varNr]->data);break;
            case CDF_DOUBLE: Proc::swapPixelsAtLocation(dataSource,(double*)var[varNr]->data);break;
            default: {CDBError("Unknown data type"); return 1;}
          }
        }
    
        #ifdef CDATAREADER_DEBUG   
        CDBDebug("--- varNR [%d], name=\"%s\"",varNr,var[varNr]->name.c_str());
        for(size_t d=0;d<var[varNr]->dimensionlinks.size();d++){
          CDBDebug("%s  \tstart: %d\tcount %d\tstride %d",var[varNr]->dimensionlinks[d]->name.c_str(),start[d],count[d],stride[d]);
        }
        #endif
      }
      dataSource->dataObject[varNr]->appliedScaleOffset = false;
      
      #ifdef MEASURETIME
      StopWatch_Stop("data read");
      #endif
      
      
      //Swap X, Y dimensions so that pointer x+y*w works correctly
      if(!cache.cacheIsAvailable()){
        //Cached data was already swapped. Because we have stored the result of this object in the cache.
        if(dataSource->swapXYDimensions){
          size_t imgSize=dataSource->dHeight*dataSource->dWidth;
          size_t w=dataSource->dWidth;size_t h=dataSource->dHeight;size_t x,y;
          void *vd=NULL;                 //destination data
          void *vs=var[varNr]->data;     //source data
        
          //Allocate data for our new memory block
          CDF::allocateData(var[varNr]->getType(),&vd,imgSize);
          //TODO This could also be solved using a template. But this works fine.
          switch (var[varNr]->getType()){
            case CDF_CHAR  : {char   *s=(char  *)vs;char   *d=(char  *)vd;for(y=0;y<h;y++)for(x=0;x<w;x++){d[x+y*w]=s[y+x*h];}}break;
            case CDF_BYTE  : {char   *s=(char  *)vs;char   *d=(char  *)vd;for(y=0;y<h;y++)for(x=0;x<w;x++){d[x+y*w]=s[y+x*h];}}break;
            case CDF_UBYTE : {uchar  *s=(uchar *)vs;uchar  *d=(uchar *)vd;for(y=0;y<h;y++)for(x=0;x<w;x++){d[x+y*w]=s[y+x*h];}}break;
            case CDF_SHORT : {short  *s=(short *)vs;short  *d=(short *)vd;for(y=0;y<h;y++)for(x=0;x<w;x++){d[x+y*w]=s[y+x*h];}}break;
            case CDF_USHORT: {ushort *s=(ushort*)vs;ushort *d=(ushort*)vd;for(y=0;y<h;y++)for(x=0;x<w;x++){d[x+y*w]=s[y+x*h];}}break;
            case CDF_INT   : {int    *s=(int   *)vs;int    *d=(int   *)vd;for(y=0;y<h;y++)for(x=0;x<w;x++){d[x+y*w]=s[y+x*h];}}break;
            case CDF_UINT  : {uint   *s=(uint  *)vs;uint   *d=(uint  *)vd;for(y=0;y<h;y++)for(x=0;x<w;x++){d[x+y*w]=s[y+x*h];}}break;
            case CDF_FLOAT : {float  *s=(float *)vs;float  *d=(float *)vd;for(y=0;y<h;y++)for(x=0;x<w;x++){d[x+y*w]=s[y+x*h];}}break;
            case CDF_DOUBLE: {double *s=(double*)vs;double *d=(double*)vd;for(y=0;y<h;y++)for(x=0;x<w;x++){d[x+y*w]=s[y+x*h];}}break;
            default: {CDBError("Unknown data type"); return 1;}
          }
          //We will replace our old memory block with the new one, but we have to free our old one first.
          free(var[varNr]->data);
          //Replace the memory block.
          var[varNr]->data=vd;
        }
      }
      
      //Apply the scale and offset factor on the data
      if(dataSource->dataObject[varNr]->appliedScaleOffset == false && dataSource->dataObject[varNr]->hasScaleOffset && cache.cacheIsAvailable() == false ){
        dataSource->dataObject[varNr]->appliedScaleOffset=true;
        
        double dfscale_factor = dataSource->dataObject[varNr]->dfscale_factor;
        double dfadd_offset = dataSource->dataObject[varNr]->dfadd_offset;
        
        #ifdef CDATAREADER_DEBUG   
        CDBDebug("Applying scale and offset with %f and %f (var size=%d) type=%s",dfscale_factor,dfadd_offset,var[varNr]->getSize(),CDF::getCDFDataTypeName(dataSource->dataObject[varNr]->cdfVariable->getType()).c_str());
        #endif
        /*if(dataSource->dataObject[varNr]->dataType==CDF_FLOAT){
          //Preserve the original nodata value, so it remains a nice short rounded number.
          float fNoData=dfNoData;
          // packed data to be unpacked to FLOAT:
          float *_data=(float*)var[varNr]->data;
          for(size_t j=0;j<var[varNr]->getSize();j++){
            //Only apply scale and offset when this actual data (do not touch the nodata)
            if(_data[j]!=fNoData){
              _data[j]*=dfscale_factor;
              _data[j]+=dfadd_offset;
            }
          }
        }*/
        if(dataSource->dataObject[varNr]->cdfVariable->getType()==CDF_FLOAT){
          //Preserve the original nodata value, so it remains a nice short rounded number.
          float fNoData=(float)dataSource->dataObject[varNr]->dfNodataValue;
          float fscale_factor=(float)dfscale_factor;
          float fadd_offset=(float)dfadd_offset;
          // packed data to be unpacked to FLOAT:
          if(fscale_factor!=1.0f||fadd_offset!=0.0f){
            float *_data=(float*)var[varNr]->data;
            size_t l=var[varNr]->getSize();
            for(size_t j=0;j<l;j++){
              _data[j]=_data[j]*fscale_factor+fadd_offset;
            }
          }
          //Convert the nodata type
          dataSource->dataObject[varNr]->dfNodataValue=fNoData*fscale_factor+fadd_offset;
        }
        
        if(dataSource->dataObject[varNr]->cdfVariable->getType()==CDF_DOUBLE){
          // packed data to be unpacked to DOUBLE:
          double *_data=(double*)var[varNr]->data;
          for(size_t j=0;j<var[varNr]->getSize();j++){
            _data[j]=_data[j]*dfscale_factor+dfadd_offset;
          }
          //Convert the nodata type
          dataSource->dataObject[varNr]->dfNodataValue=dataSource->dataObject[varNr]->dfNodataValue*dfscale_factor+dfadd_offset;
        }
      }
      
      #ifdef MEASURETIME
      StopWatch_Stop("Scale and offset applied");
      #endif
      

   
      
   /**
    *Cache is deprecated since 2014-01-01
    *   
//       In order to write good cache files we need to modify
//         our cdfobject. Data is now unpacked, so we need to remove
//         scale_factor and add_offset attributes and change datatypes 
//         of the data and _FillValue 
//       
      //remove scale_factor and add_offset attributes, otherwise they are stored in the cachefile again and reapplied over and over again.
      var[varNr]->removeAttribute("scale_factor");
      var[varNr]->removeAttribute("add_offset");
      
      //Set original var datatype correctly for the cdfobject 
      //var[varNr]->getType()=dataSource->dataObject[varNr]->dataType;
      
      //Reset _FillValue to correct datatype and adjust scale and offset values.
      if( dataSource->dataObject[varNr]->hasNodataValue){
        CDF::Attribute *fillValue = var[varNr]->getAttributeNE("_FillValue");
        if(fillValue!=NULL){
          if(var[varNr]->getType()==CDF_FLOAT){float fNoData=(float)dataSource->dataObject[varNr]->dfNodataValue;fillValue->setData(CDF_FLOAT,&fNoData,1);}
          if(var[varNr]->getType()==CDF_DOUBLE)fillValue->setData(CDF_DOUBLE,&dataSource->dataObject[varNr]->dfNodataValue,1);
        }
      }*/
    }

    

    if(dataSource->stretchMinMax){//&&((dataSource->dWidth!=2||dataSource->dHeight!=2))){
      if(dataSource->stretchMinMaxDone == false){
        if(dataSource->statistics==NULL){
          #ifdef CDATAREADER_DEBUG
          CDBDebug("No statistics available");
          #endif    
          dataSource->statistics = new CDataSource::Statistics();
          dataSource->statistics->calculate(dataSource);
          #ifdef MEASURETIME
          StopWatch_Stop("Calculated statistics");
          #endif
        }
        float min=(float)dataSource->statistics->getMinimum();
        float max=(float)dataSource->statistics->getMaximum();
        #ifdef CDATAREADER_DEBUG    
          CDBDebug("Statistics: Min = %f, Max = %f",min,max);
        #endif    
        
        //Make sure that there is always a range in between the min and max.
        if(max==min)max=min+0.1;
        //Calculate legendOffset legendScale
        float ls=240/(max-min);
        float lo=-(min*ls);
        dataSource->legendScale=ls;
        dataSource->legendOffset=lo;
        dataSource->stretchMinMaxDone = true;
      }
    }
    
    //Check for infinities
    if(dataSource->legendScale!=dataSource->legendScale||
      dataSource->legendScale==INFINITY||
      dataSource->legendScale==NAN||
      dataSource->legendScale==-INFINITY){
      dataSource->legendScale=240;
    }
    if(dataSource->legendOffset!=dataSource->legendOffset||
      dataSource->legendOffset==INFINITY||
      dataSource->legendOffset==NAN||
      dataSource->legendOffset==-INFINITY){
      dataSource->legendOffset=0;
    }
    
    #ifdef CDATAREADER_DEBUG    
    CDBDebug("dataSource->legendScale = %f, dataSource->legendOffset = %f",dataSource->legendScale,dataSource->legendOffset);
    #endif    
    
  #ifdef MEASURETIME
    StopWatch_Stop("all read");
  #endif

    
    //DataPostProc: Here our datapostprocessor comes into action!
    for(size_t dpi=0;dpi<dataSource->cfgLayer->DataPostProc.size();dpi++){
      CServerConfig::XMLE_DataPostProc * proc = dataSource->cfgLayer->DataPostProc[dpi];
      
      //Algorithm msgcppmask
      if(proc->attr.algorithm.equals("msgcppmask")){
        if(dataSource->dataObject.size()!=2){
          CDBError("3 variables are needed for msgcppmask");
        }
        CDBDebug("Applying msgcppmask");
        float *data1=(float*)dataSource->dataObject[0]->cdfVariable->data;//sunz
        float *data2=(float*)dataSource->dataObject[1]->cdfVariable->data;
        //float *data3=(float*)dataSource->dataObject[2]->data;//azidiff
        float fNodata1=(float)dataSource->dataObject[0]->dfNodataValue;
        //float fNodata3=(float)dataSource->dataObject[2]->dfNodataValue;
        size_t l=(size_t)dataSource->dHeight*(size_t)dataSource->dWidth;
        
        float fa=72,fb=75; 
        
        if(proc->attr.b.empty()==false){CT::string b;b.copy(proc->attr.b.c_str());fb=b.toDouble();}
        if(proc->attr.a.empty()==false){CT::string a;a.copy(proc->attr.a.c_str());fa=a.toDouble();}
        
        for(size_t j=0;j<l;j++){
          //if((data2[j]<72&&data1[j]<72&&data3[j]!=fNodata3)||(data2[j]>75))data1[j]=fNodata1;//else data1[j]=1;
          if((data2[j]<fa&&data1[j]<fa)||(data2[j]>fb))data1[j]=fNodata1; else data1[j]=1;
        //  if(data1[j]<43||data1[j]>65)data1[j]=fNodata;//else data1[j]=1;
          //if(data3[j]+data1[j]<60)data1[j]=fNodata;;
        // if(data1[j]<data2[j]-25)data1[j]=fNodata;;
        //  if(data1[j]>data2[j]+25)data1[j]=fNodata;;
        // if((data1[j]*data1[j]*data1[j]+data2[j]*data2[j]*data2[j])>55*55*data2[j])data1[j]=fNodata;
          //data1[j]=sqrt(data1[j]*data1[j]+data2[j]*data2[j]);
        }
        delete dataSource->dataObject[1];
        //delete dataSource->dataObject[2];
        //dataSource->dataObject.erase(dataSource->dataObject.begin()+1);
        dataSource->dataObject.erase(dataSource->dataObject.begin()+1);
      }
      #ifdef MEASURETIME
      StopWatch_Stop("Data postprocessing completed");
      #endif
    }
  
    //CDBDebug("Cache: %d %d %d",enableDataCache,cache.cacheIsAvailable(),cache.saveCacheFile());
    //Write the cache file if neccessary.
    if(enableDataCache==true&&cache.saveCacheFile()&&mode == CNETCDFREADER_MODE_OPEN_ALL){
      
      /*int cacheStatus = cache.claimCacheFile();
      if(cacheStatus!=0){
        //CDBError("Cache file could not be claimed: %d %s",cacheStatus,cache.getCacheFileNameToWrite());
        return 0;
      }
      if(cache.saveCacheFile()==false){
        CDBError("Cache file could not be saved");
        return 0;
      }*/
      int keepVariable[cdfObject->variables.size()];
      int keepDimension[cdfObject->dimensions.size()];
      //By default throw away everything
      for(size_t v=0;v<cdfObject->variables.size();v++)keepVariable[v]=0;
      for(size_t v=0;v<cdfObject->dimensions.size();v++)keepDimension[v]=0;        
      //CDBDebug("Writing to %s",cacheFilename.c_str());

      //keep the concerning variable itself
      for(size_t v=0;v<dataSource->dataObject.size();v++){
        CDF::Variable * var=dataSource->dataObject[v]->cdfVariable;
        try{
          keepVariable[cdfObject->getVariableIndex(var->name.c_str())]=1;
        }catch(int e){}
      }
      
      //Make a list of required dimensions
      for(size_t v=0;v<cdfObject->variables.size();v++){
        try{
          if(keepVariable[v]==1){
            //Loop through dim links
            CDF::Variable *var = cdfObject->variables[v];
            for(size_t d=0;d<var->dimensionlinks.size();d++){
              try{
                keepDimension[cdfObject->getDimensionIndex(var->dimensionlinks[d]->name.c_str())]=1;
              }catch(int e){
                CT::string errMsg;
                CDF::getErrorMessage(&errMsg,e);
                CDBDebug("Dim %s %s",var->dimensionlinks[d]->name.c_str(),errMsg.c_str());
                
              }
              try{
                keepVariable[cdfObject->getVariableIndex(var->dimensionlinks[d]->name.c_str())]=1;
              }catch(int e){
                CT::string errMsg;
                CDF::getErrorMessage(&errMsg,e);
                CDBDebug("Var %s %s",var->dimensionlinks[d]->name.c_str(),errMsg.c_str());
                
              }                  
            }
          }
        }catch(int e){}
      }
      
      //Make a list of required variables
      for(size_t v=0;v<dataSource->dataObject.size();v++){
        for(size_t w=0;w<cdfObject->variables.size();w++){
          //Metadata variables often have no dimensions. We want to keep them to show info to the user.
          if(keepVariable[w]==0){
            if(cdfObject->variables[w]->dimensionlinks.size()<2){
              keepVariable[w]=1;
              cdfObject->variables[w]->dimensionlinks.resize(0);
            }
          }
        }
      }

      //Remove variables
      std::vector<CT::string *> deleteVarNames;
      for(size_t v=0;v<cdfObject->variables.size();v++){
        if(keepVariable[v]==0)deleteVarNames.push_back(new CT::string(&cdfObject->variables[v]->name));
      }
      for(size_t i=0;i<deleteVarNames.size();i++){

        cdfObject->removeVariable(deleteVarNames[i]->c_str());
        
        delete deleteVarNames[i];
      }
      deleteVarNames.clear();
      
      //Remove dimensions
      for(size_t d=0;d<cdfObject->dimensions.size();d++){
        if(keepDimension[d]==0)deleteVarNames.push_back(new CT::string(&cdfObject->dimensions[d]->name));
      }
      for(size_t i=0;i<deleteVarNames.size();i++){
        //CDBDebug("Removing dimensions %s",deleteVarNames[i]->c_str());
        cdfObject->removeDimension(deleteVarNames[i]->c_str());
        cdfObject->removeVariable(deleteVarNames[i]->c_str());
        delete deleteVarNames[i];
      }
      
      //Adjust dimensions for the single object:
      dataSource->varX->setType(CDF_DOUBLE);
      dataSource->varY->setType(CDF_DOUBLE);
      
      //Adjust fillvalues
      
      try{
        double fillValueVarX;
        dataSource->varX->getAttribute("_FillValue")->getData(&fillValueVarX,1);
        dataSource->varX->setAttribute("_FillValue",CDF_DOUBLE,&fillValueVarX,1);
      }catch(int e){
      }
       
      try{
        double fillValueVarY;
        dataSource->varY->getAttribute("_FillValue")->getData(&fillValueVarY,1);
        dataSource->varY->setAttribute("_FillValue",CDF_DOUBLE,&fillValueVarY,1);
      }catch(int e){
      }
      
      try{
        //Reduce dimension sizes because of striding
        cdfObject->getDimension(dataSource->varX->name.c_str())->setSize(cdfObject->getDimension(dataSource->varX->name.c_str())->getSize()/dataSource->stride2DMap);
        cdfObject->getDimension(dataSource->varY->name.c_str())->setSize(cdfObject->getDimension(dataSource->varY->name.c_str())->getSize()/dataSource->stride2DMap);
        //CDBDebug("dfdim_X[0] %f-%f",dfdim_X[0],dfdim_X[dataSource->dWidth-1]);
        double *dfdim_X=(double*)dataSource->varX->data;
        double *dfdim_Y=(double*)dataSource->varY->data;
        for(size_t j=0;j<cdfObject->getDimension(dataSource->varX->name.c_str())->getSize();j++)((double*)dataSource->varX->data)[j]=dfdim_X[j];
        for(size_t j=0;j<cdfObject->getDimension(dataSource->varY->name.c_str())->getSize();j++)((double*)dataSource->varY->data)[j]=dfdim_Y[j];
      }catch(int e){}
      
      //Read the dimensions
      int timeStep = dataSource->getCurrentTimeStep();
      for(size_t j=0;j<dataSource->timeSteps[timeStep]->dims.dimensions.size();j++){
        CDF::Dimension *dim = cdfObject->getDimensionNE(dataSource->timeSteps[timeStep]->dims.dimensions[j]->name.c_str());
        if(dim!=NULL){
          CDF::Variable *var = cdfObject->getVariableNE(dataSource->timeSteps[timeStep]->dims.dimensions[j]->name.c_str());
          if(var==NULL){CDBError("var is null");return 1;}
          //CDBDebug("Cache Reading variable %s"    ,var->name.c_str());
          if(var->readData(CDF_DOUBLE)!=0){
            CDBError("Unable to read variable %s",var->name.c_str());
            return 1;
          }
          try{
            double dimFillValue;
            var->getAttribute("_FillValue")->getData(&dimFillValue,1);
            var->setAttribute("_FillValue",CDF_DOUBLE,&dimFillValue,1);
          }catch(int e){
          }
          double dimValue[var->getSize()];
          CDF::DataCopier::copy(dimValue,var->data,var->getType(),var->getSize());
          size_t index=dataSource->timeSteps[timeStep]->dims.dimensions[0]->index;
          var->setSize(1);
          dim->length=1;
          ((double*)var->data)[0]=dimValue[index];
        }
      }

      //Write cachefile
      
      CDFNetCDFWriter netCDFWriter(cdfObject);
      netCDFWriter.disableReadData();
      netCDFWriter.setNetCDFMode(4);
      //TODO disable compression
      //netCDFWriter.setDeflate(0);
      try{
        if(netCDFWriter.write(cache.getCacheFileNameToWrite())!=0){
          CDBError("Unable to write cachefile");
          cache.removeClaimedCachefile();
          return 0;
        }
      }catch(int e){
        CDBError("Exception %d in writing cache file",e);
        cache.removeClaimedCachefile();
        return 1;
      }
      cache.releaseCacheFile();
    
      return 0;
    }
  }
  #ifdef CDATAREADER_DEBUG
    CDBDebug("/Finished datareader");
  #endif
  return 0;
}

int CDataReader::getTimeDimIndex( CDFObject *cdfObject, CDF::Variable * var){
  if(var == NULL || cdfObject == NULL)return -1;
  if(var->dimensionlinks.size()==0){return -1;}
  CT::string standard_name;
  //First try to retrieve the time dimension by standard_name
  for(size_t j=0;j<var->dimensionlinks.size();j++){
    try{
      cdfObject->getVariable(var->dimensionlinks[j]->name.c_str())->getAttribute("standard_name")->getDataAsString(&standard_name);
      if(standard_name.equals("time"))return j;
    }catch(int e){
    }
  }
  //Second, if failed try to find it based on variable name.
  for(size_t j=0;j<var->dimensionlinks.size();j++){
    if(var->dimensionlinks[j]->name.equals("time"))return j;
  }
  //Third, try to find it based on indexof method.
  for(size_t j=0;j<var->dimensionlinks.size();j++){
    if(var->dimensionlinks[j]->name.indexOf("time")>=0)return j;
  }
  return -1;
}
CDF::Variable *CDataReader::getTimeVariable( CDFObject *cdfObject, CDF::Variable * var){
  int timeDimIndex = getTimeDimIndex(cdfObject,var);
  if(timeDimIndex == -1) return NULL;
  return cdfObject->getVariableNE(var->dimensionlinks[timeDimIndex]->name.c_str());
}


CT::string CDataReader::getTimeUnit(CDataSource *dataSource){
  CDF::Variable *time = getTimeVariable(dataSource->dataObject[0]->cdfObject,dataSource->dataObject[0]->cdfVariable);
  if(time == NULL){CDBDebug("No time variable found");throw(1);}
  CDF::Attribute *timeUnits = time->getAttributeNE("units");
  if(timeUnits == NULL){CDBDebug("No time units found");throw(2);}
  
  CT::string timeUnitsString=timeUnits->toString().c_str();
  
  return timeUnitsString;
}

int CDataReader::getTimeString(CDataSource *dataSource,char * pszTime){
  //TODO We assume that the first configured DIM is always time. This might be not the case!
  pszTime[0]='\0';
  if(dataSource->isConfigured==false){
    CDBError("dataSource is not configured");
    return 1;
  }
  if(dataSource->cfgLayer->Dimension.size()==0){
    snprintf(pszTime,MAX_STR_LEN,"No time dimension available");
    CDBDebug("%s",pszTime);
    return 1;
  }
  CDF::Variable *time = getTimeVariable(dataSource->dataObject[0]->cdfObject,dataSource->dataObject[0]->cdfVariable);
  if(time==NULL){CDBDebug("No time variable found");return 1;}
  CDF::Attribute *timeUnits = time->getAttributeNE("units");
  if(timeUnits ==NULL){CDBDebug("No time units found");return 1;}
  time->readData(CDF_DOUBLE);
  if(dataSource->dNetCDFNumDims>2){
    size_t currentTimeIndex=dataSource->getDimensionIndex(time->name.c_str());
    if(currentTimeIndex>=0&&currentTimeIndex<time->getSize()){
      CTime adagucTime;
      try{
        adagucTime.init(timeUnits->toString().c_str());
        CT::string isoString = "No time dimension available";
        try{
          adagucTime.dateToISOString(adagucTime.getDate(((double*)time->data)[currentTimeIndex]));
        }catch(int e){
        }
        snprintf(pszTime,MAX_STR_LEN,"%s",isoString.c_str());
        
      }catch(int e){
        CDBError("Unable to initialize CTime with units %s",timeUnits->toString().c_str());
        return 1;
      }

    }else{
      CDBDebug("time index out of bounds");
      snprintf(pszTime,MAX_STR_LEN,"No time dimension available");
      return 1;
    }
  }else{
    snprintf(pszTime,MAX_STR_LEN,"No time dimension available");
    return 1;
  }
  //CDBDebug("[OK] pszTime = %s",pszTime);
  return 0;
}



int CDataReader::justLoadAFileHeader(CDataSource *dataSource){
  
  if(dataSource==NULL){CDBError("datasource == NULL");return 1;}
  if(dataSource->cfgLayer==NULL){CDBError("datasource->cfgLayer == NULL");return 1;}
  if(dataSource->dataObject.size()==0){CDBError("dataSource->dataObject.size()==0");return 1;}
  if(dataSource->dataObject[0]->cdfVariable!=NULL){
    #ifdef CDATAREADER_DEBUG  
    CDBDebug("already loaded: dataSource->dataObject[0]->cdfVariable!=NULL");
    #endif
    return 0;
  }
  
  const char *fileName = NULL;
  
  fileName = dataSource->getFileName();
  //CDBDebug("Loading header [%s]",fileName);
  
  CDirReader dirReader; // Must stay outside the next statement in order to keep the pointer to fileName sane.
  if(fileName == NULL){
    
    if(CDBFileScanner::searchFileNames(&dirReader,dataSource->cfgLayer->FilePath[0]->value.c_str(),dataSource->cfgLayer->FilePath[0]->attr.filter.c_str(),NULL)!=0){CDBError("Could not find any filename");return 1; }
    if(dirReader.fileList.size()==0){CDBError("dirReader.fileList.size()==0");return 1; }
    
    fileName = dirReader.fileList[0]->fullName.c_str();
    //CDBDebug("Loading header [%s]",fileName);
  }
  
  
  //Open a file
  try{
    //CDBDebug("Loading header [%s]",fileName);
    CDFObject *cdfObject = CDFObjectStore::getCDFObjectStore()->getCDFObjectHeader(dataSource->srvParams,fileName);
    if(cdfObject == NULL)throw(__LINE__);


    
   dataSource->attachCDFObject(cdfObject);
   
  }catch(int linenr){
    CDBError("Returning from line %d");
    dataSource->detachCDFObject();
    return 1;        
  }
  
  return 0;
}


int CDataReader::autoConfigureDimensions(CDataSource *dataSource){
  
  #ifdef CDATAREADER_DEBUG        
  CDBDebug("[autoConfigureDimensions]");
  #endif     
  if(dataSource->dLayerType==CConfigReaderLayerTypeCascaded){
    #ifdef CDATAREADER_DEBUG        
    CDBDebug("Cascaded layers cannot have dimensions at the moment");
    #endif     
    return 0;
  }

  // Auto configure dimensions, in case they are not configured by the user.
  // Dimension configuration is added to the internal XML configuration structure.
  if(dataSource->cfgLayer->Dimension.size()>0){
#ifdef CDATAREADER_DEBUG        
        CDBDebug("dataSource->cfgLayer->Dimension.size()>0: return 0;");
#endif     
    return 0;
  }
  if(dataSource==NULL){CDBDebug("datasource == NULL");return 1;}
  if(dataSource->cfgLayer==NULL){CDBDebug("datasource->cfgLayer == NULL");return 1;}

  
#ifdef CDATAREADER_DEBUG
 CDBDebug("autoConfigureDimensions %s",dataSource->getLayerName());
#endif  
  
 /** 
  * Load dimension information about the layer from the autoconfigure_dimensions table
  * This table stores only layerid, netcdf dimname, adaguc dimname and units
  * Actual dimension values are not storen in this table
  */
  CT::string query;
  CT::string tableName = "autoconfigure_dimensions";
  
  CT::string layerTableId;
  try{
    layerTableId = dataSource->srvParams->lookupTableName(dataSource->cfgLayer->FilePath[0]->value.c_str(),dataSource->cfgLayer->FilePath[0]->attr.filter.c_str(), NULL);
  }catch(int e){
    CDBError("Unable to get layerTableId for autoconfigure_dimensions");
    return 1;
  }
  
  CCache::Lock lock;
  CT::string identifier = "autoConfigureDimensions";  identifier.concat(dataSource->cfgLayer->FilePath[0]->value.c_str());  identifier.concat("/");  identifier.concat(dataSource->cfgLayer->FilePath[0]->attr.filter.c_str());  
  CT::string cacheDirectory = "";
  dataSource->srvParams->getCacheDirectory(&cacheDirectory);
  if(cacheDirectory.length()>0){
    lock.claim(cacheDirectory.c_str(),identifier.c_str(),dataSource->srvParams->isAutoResourceEnabled());
  }

  
  layerTableId.concat("_");layerTableId.concat(dataSource->getLayerName());
  query.print("SELECT * FROM %s where layerid=E'%s'",tableName.c_str(),layerTableId.c_str());
  CPGSQLDB db;

  if(db.connect(dataSource->cfg->DataBase[0]->attr.parameters.c_str())!=0){CDBError("Error Could not connect to the database");return 1; }
  CDB::Store *store = db.queryToStore(query.c_str());
  db.close(); 
  if(store!=NULL){
    
    try{
      
      for(size_t j=0;j<store->size();j++){
        CServerConfig::XMLE_Dimension *xmleDim=new CServerConfig::XMLE_Dimension();
        
        xmleDim->value.copy(store->getRecord(j)->get("ogcname")->c_str());
        xmleDim->attr.name.copy(store->getRecord(j)->get("ncname")->c_str());
        xmleDim->attr.units.copy(store->getRecord(j)->get("units")->c_str());
#ifdef CDATAREADER_DEBUG        
        CDBDebug("Retrieved auto dim %s-%s from db for layer %s", xmleDim->value.c_str(),xmleDim->attr.name.c_str(),layerTableId.c_str());
#endif        
        dataSource->cfgLayer->Dimension.push_back(xmleDim);
      }
      size_t storeSize = store->size();
      delete store;
      if(storeSize>0){
        #ifdef CDATAREADER_DEBUG        
         CDBDebug("Datasource has the following extra dims (%d):",dataSource->cfgLayer->Dimension.size());
         for(size_t j=0;j<dataSource->cfgLayer->Dimension.size();j++){
           CDBDebug("  [%d]: Dimension name\"%s\"\t units\"%s\"",j,dataSource->cfgLayer->Dimension[j]->attr.name.c_str(),dataSource->cfgLayer->Dimension[j]->attr.units.c_str());
         }
         CDBDebug("Found auto dims in DB, returning from line %d.",__LINE__);
        #endif        
      return 0;
      }
    }catch(int e){
      delete store;
      CDBError("DB Exception: %s\n",db.getErrorMessage(e));
    }
  }
#ifdef CDATAREADER_DEBUG     
  CDBDebug("autoConfigureDimensions information not in table %s",tableName.c_str());
#endif  
  
  /* Dimension information is not available in the database. We need to load it from a file.*/
  #ifdef CDATAREADER_DEBUG     
  CDBDebug("justLoadAFileHeader");
  #endif
  
  int status=justLoadAFileHeader(dataSource);
  if(status!=0){
    CDBError("justLoadAFileHeader failed");
    return 1;
  }
  
//  CDBDebug("Dimensions: %d - %d",dataSource->cfgLayer->Dimension.size(),dataSource->dataObject[0]->cdfVariable->dimensionlinks.size());
  
  try{
    if(dataSource->dataObject.size()==0){CDBDebug("dataSource->dataObject.size()==0");throw(__LINE__);}
    if(dataSource->dataObject[0]->cdfVariable==NULL){CDBDebug("dataSource->dataObject[0]->cdfVariable==NULL");throw(__LINE__);}
    //CDBDebug("OK %d",dataSource->cfgLayer->Dimension.size());
   
    if(dataSource->cfgLayer->Dimension.size()==0){
      
      
      /*No dimensions are configured by the user, try to detect them from the netcdf file automatically
       and store them in the table.
       */
      if(dataSource->dataObject[0]->cdfVariable->dimensionlinks.size()>=2){
        
        try{
          //Create the database table
          CT::string tableColumns("layerid varchar (255), ncname varchar (255), ogcname varchar (255), units varchar (255)");
          int status;
          CPGSQLDB DB;
          status = DB.connect(dataSource->cfg->DataBase[0]->attr.parameters.c_str());if(status!=0){CDBError("Error Could not connect to the database");throw(__LINE__);}
          status = DB.checkTable(tableName.c_str(),tableColumns.c_str());
          if(status == 1){CDBError("\nFAIL: Table %s could not be created: %s",tableName.c_str(),tableColumns.c_str()); DB.close();throw(__LINE__);  }
          
          CDF::Variable *variable=dataSource->dataObject[0]->cdfVariable;
          //CDBDebug("OK %d",variable->dimensionlinks.size()-2);
          
          // When there are no extra dims besides x and y: make a table anyway so autoconfigure_dimensions is not run 
          // Each time to find the non existing dims.
          if(variable->dimensionlinks.size()==2){
            #ifdef CDATAREADER_DEBUG  
            CDBDebug("Creating an empty table, because variable %s has only x and y dims",variable->name.c_str());
            #endif
            return 0;
          }
          
          for(size_t d=0;d<variable->dimensionlinks.size()-2;d++){
            
            CDF::Dimension *dim=variable->dimensionlinks[d];
            if(dim!=NULL){
              CDF::Variable *dimVar=dataSource->dataObject[0]->cdfObject->getVariable(dim->name.c_str());
              
              CT::string units="";
              CT::string standard_name=dim->name.c_str();  
              CT::string OGCDimName;
              try{dimVar->getAttribute("units")->getDataAsString(&units);}catch(int e){}
              //try{dimVar->getAttribute("standard_name")->getDataAsString(&standard_name);}catch(int e){}
              OGCDimName.copy(&standard_name);
              #ifdef CDATAREADER_DEBUG  
              CDBDebug("Datasource %s: Dim %s; units %s; standard_name %s",dataSource->layerName.c_str(),dim->name.c_str(),units.c_str(),standard_name.c_str());
              #endif
              CServerConfig::XMLE_Dimension *xmleDim=new CServerConfig::XMLE_Dimension();
              dataSource->cfgLayer->Dimension.push_back(xmleDim);
              xmleDim->value.copy(OGCDimName.c_str());
              xmleDim->attr.name.copy(standard_name.c_str());
              xmleDim->attr.units.copy(units.c_str());
              
              
              //Store the data in the db for quick access.
              query.print("INSERT INTO %s values (E'%s',E'%s',E'%s',E'%s')",tableName.c_str(),layerTableId.c_str(),standard_name.c_str(),OGCDimName.c_str(),units.c_str());
              status = DB.query(query.c_str()); if(status!=0){CDBError("Unable to insert records: \"%s\"",query.c_str());DB.close();throw(__LINE__); }
                      
            }else{
              CDBDebug("variable->dimensionlinks[d]");
            }
          }
          
        }catch(int e){
          CT::string errorMessage;CDF::getErrorMessage(&errorMessage,e); CDBDebug("Unable to configure dims automatically: %s (%d)",errorMessage.c_str(),e);    
          throw(e);
        }
      }
      
     
    }
  }catch(int linenr){
    CDBDebug("Exp at line %d",linenr);

    return 1;
  }

  #ifdef CDATAREADER_DEBUG        
  CDBDebug("Datasource has the following dims (%d):",dataSource->cfgLayer->Dimension.size());
  for(size_t j=0;j<dataSource->cfgLayer->Dimension.size();j++){
    CDBDebug("  [%d]: Dimension name\"%s\"\t units\"%s\"",j,dataSource->cfgLayer->Dimension[j]->attr.name.c_str(),dataSource->cfgLayer->Dimension[j]->attr.units.c_str());
  }
  CDBDebug("Found auto dims from NC file, returning from line %d.",__LINE__);
  #endif       
  lock.release();
  return 0;
}


int CDataReader::autoConfigureStyles(CDataSource *dataSource){
  
  if(dataSource->dLayerType==CConfigReaderLayerTypeCascaded){
    #ifdef CDATAREADER_DEBUG        
    CDBDebug("Cascaded layers cannot have styles at the moment");
    #endif     
    return 0;
  }
  
  bool useDBCache = false;
#ifdef CDATAREADER_DEBUG     
  CDBDebug("autoConfigureStyles");
#endif  
  if(dataSource==NULL){CDBDebug("datasource == NULL");return 1;}
  if(dataSource->cfgLayer==NULL){CDBDebug("datasource->cfgLayer == NULL");return 1;}
  if(dataSource->cfgLayer->Styles.size()!=0){return 0;};//Configured by user, auto styles is not required

  if(dataSource->cfgLayer->Legend.size()!=0){return 0;};//Configured by user, auto styles is not required

  
  
  // Try to find a style corresponding the the standard_name attribute of the file.
  CServerConfig::XMLE_Styles *xmleStyle=new CServerConfig::XMLE_Styles();
  dataSource->cfgLayer->Styles.push_back(xmleStyle);
  
  CT::string tableName = "autoconfigure_styles";
  CT::string layerTableId;
  try{
    layerTableId = dataSource->srvParams->lookupTableName(dataSource->cfgLayer->FilePath[0]->value.c_str(),dataSource->cfgLayer->FilePath[0]->attr.filter.c_str(), NULL);
  }catch(int e){
    CDBError("Unable to get layerTableId for autoconfigure_styles");
    return 1;
  }
  CT::string query;
  
  if(useDBCache){
    
    query.print("SELECT * FROM %s where layerid=E'%s'",tableName.c_str(),layerTableId.c_str());
    CPGSQLDB db;
    if(db.connect(dataSource->cfg->DataBase[0]->attr.parameters.c_str())!=0){CDBError("Error Could not connect to the database");return 1; }
    CDB::Store *store = db.queryToStore(query.c_str());
    db.close(); 
    if(store!=NULL){
      if(store->size()!=0){
        try{
          xmleStyle->value.copy(store->getRecord(0)->get("styles")->c_str());
          delete store;store=NULL;
        }catch(int e){
          delete store;
          CDBError("autoConfigureStyles: DB Exception: %s for query %s",db.getErrorMessage(e),query.c_str());
          return 1;
        }
  #ifdef CDATAREADER_DEBUG           
        CDBDebug("Retrieved auto styles \"%s\" from db",xmleStyle->value.c_str());
  #endif     
        //OK!
        return 0;
      }
      delete store;
    }
  }
  
    
    // Auto style is not available in the database, so look it up in the file.
    
    
    //If the file header is not yet loaded, load it.
    if(dataSource->dataObject[0]->cdfVariable==NULL){
      #ifdef CDATAREADER_DEBUG     
      CDBDebug("justLoadAFileHeader");
      #endif
      int status=CDataReader::justLoadAFileHeader(dataSource);
      if(status!=0){CDBError("unable to load datasource headers");return 1;}
    }
    
    //Get the searchname, based on variable, standard name or long name.
    CT::string searchName=dataSource->dataObject[0]->variableName.c_str();
    try{dataSource->dataObject[0]->cdfVariable->getAttribute("standard_name")->getDataAsString(&searchName);}catch(int e){}
    
    searchName.toLowerCaseSelf();
    
    //Get the units
    CT::string dataSourceUnits;
    if(dataSource->dataObject[0]->units.length()>0){
      dataSourceUnits = dataSource->dataObject[0]->units.c_str();
    }
    dataSourceUnits.toLowerCaseSelf();
    
    #ifdef CDATAREADER_DEBUG  
    CDBDebug("Retrieving auto styles by using fileinfo \"%s\"",searchName.c_str());
    #endif
    // We now have the keyword searchname, with this keyword we are going to lookup all StandardName's in the server configured Styles
    
    
    std::vector<CT::string> styleList;
    
    for(size_t j=0;j<dataSource->cfg->Style.size();j++){
      
      const char *styleName=dataSource->cfg->Style[j]->attr.name.c_str();
      #ifdef CDATAREADER_DEBUG  
      CDBDebug("Searching Style \"%s\"",styleName);
      #endif
      if(styleName!=NULL)
        {
        for(size_t i=0;i<dataSource->cfg->Style[j]->StandardNames.size();i++){
          
          CT::string standard_name;
          CT::string units;
          
          if(dataSource->cfg->Style[j]->StandardNames[i]->attr.standard_name.empty()==false){
            
            standard_name.copy(dataSource->cfg->Style[j]->StandardNames[i]->attr.standard_name.c_str());
          }
          
          standard_name.toLowerCaseSelf();
          //standard_name.replaceSelf("_"," ");
          
          if(dataSource->cfg->Style[j]->StandardNames[i]->attr.units.empty()==false){
            
            units.copy(dataSource->cfg->Style[j]->StandardNames[i]->attr.units.c_str());
          }
          units.toLowerCaseSelf();
          
          #ifdef CDATAREADER_DEBUG  
          CDBDebug("Searching StandardNames \"%s\"",standard_name.c_str());
          #endif
          if(standard_name.length()>0){
            //CT::stringlist *standardNameList=standard_name.splitN(",");
            CT::StackList<CT::string> standardNameList=standard_name.splitToStack(",");
            for(size_t n=0;n<standardNameList.size();n++){
              if(searchName.equals(standardNameList[n].c_str())){
                bool unitsMatch = true;
                if(dataSourceUnits.length()!=0&&units.length()!=0){
                  unitsMatch = false;
                  if(dataSourceUnits.equals(&units))unitsMatch = true;
                }
                if(unitsMatch){
                  #ifdef CDATAREADER_DEBUG  
                  CDBDebug("*** Match: \"%s\"== \"%s\"",searchName.c_str(),standardNameList[n].c_str());
                  #endif
                  styleList.push_back(dataSource->cfg->Style[j]->attr.name.c_str());
                  //if(styles.length()!=0)styles.concat(",");
                  //styles.concat(dataSource->cfg->Style[j]->attr.name.c_str());
                }
              }
            }
          }
          if(standard_name.equals("*")){
            //if(styles.length()!=0)styles.concat(",");
            //styles.concat(dataSource->cfg->Style[j]->attr.name.c_str());
            styleList.push_back(dataSource->cfg->Style[j]->attr.name.c_str());
          }
        }
      }     
    }
    
    
    
    CT::string styles="";
    
    for(size_t j=0;j<styleList.size();j++){
       if(styles.length()!=0)styles.concat(",");
       styles.concat(styleList[j].c_str());
    }
    
    if(styles.length()==0) styles="auto";
    
    //CDBDebug("Found styles \"%s\"",styles.c_str());
    if(useDBCache){
      //Put the result back into the database.
      try{
        CT::string tableColumns("layerid varchar (255), styles varchar (255)");
        int status;
        CPGSQLDB DB;
        status = DB.connect(dataSource->cfg->DataBase[0]->attr.parameters.c_str());if(status!=0){CDBError("Error Could not connect to the database");throw(__LINE__);}
        status = DB.checkTable(tableName.c_str(),tableColumns.c_str());
        if(status == 1){CDBError("\nFAIL: Table %s could not be created: %s",tableName.c_str(),tableColumns.c_str()); DB.close();throw(__LINE__);  }
        query.print("INSERT INTO %s values (E'%s',E'%s')",tableName.c_str(),layerTableId.c_str(),styles.c_str());
        status = DB.query(query.c_str()); if(status!=0){CDBError("Unable to insert records: \"%s\"",query.c_str());DB.close();throw(__LINE__); }
      }catch(int linenr){
        CDBError("Exception at line %d",linenr);
        return 1;
      }
    }
    xmleStyle->value.copy(styles.c_str());
  
  return 0;
}

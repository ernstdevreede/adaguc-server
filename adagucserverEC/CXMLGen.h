#ifndef CXMLGen_H
#define CXMLGen_H
#include "CServerParams.h"
#include <stdio.h>
#include <string.h>


#include <CDefinitions.h>
#include "CServerError.h"
#include "CDataReader.h"
#include "CImageWarper.h"
#include "CDrawImage.h"
#include "CPGSQLDB.h"
#include "CDataSource.h"
#include "CDebugger.h"
#include "CImageDataWriter.h"
#include "CRequest.h"





class WMSLayer{
  public:
    class Dim{
      public:
        CT::string name;
        CT::string units;
        CT::string values;
        CT::string defaultValue;
        int hasMultipleValues;
    };
    class Projection{
      public:
        CT::string name;
        double dfBBOX[4];
    };
    class Style{
      public:
        CT::string name;
    };
    WMSLayer(){
      isQuerable=0;
      hasError=0;
      dataSource=NULL;
    }
    ~WMSLayer(){
      delete dataSource;
      for(size_t j=0;j<projectionList.size();j++){delete projectionList[j];projectionList[j]=NULL;}
      for(size_t j=0;j<dimList.size();j++){delete dimList[j];dimList[j]=NULL;}
      for(size_t j=0;j<styleList.size();j++){delete styleList[j];styleList[j]=NULL;}
    }
    CServerConfig::XMLE_Layer * layer;
    CT::string name,title,group,fileName;
    int isQuerable,hasError;
    CDataSource *dataSource;
    std::vector<Projection*> projectionList;
    std::vector<Dim*> dimList;
    std::vector<Style*> styleList;
    double dfLatLonBBOX[4];    
    
};


class CXMLGen{
  private:
    DEF_ERRORFUNCTION();
    int getFileNameForLayer(WMSLayer * myWMSLayer);
    int getDataSourceForLayer(WMSLayer * myWMSLayer);
    int getProjectionInformationForLayer(WMSLayer * myWMSLayer);
    int getDimsForLayer(WMSLayer * myWMSLayer);
    int getWMS_1_0_0_Capabilities(CT::string *XMLDoc,std::vector<WMSLayer*> *myWMSLayerList);
    int getWMS_1_1_1_Capabilities(CT::string *XMLDoc,std::vector<WMSLayer*> *myWMSLayerList);
    int getWCS_1_0_0_Capabilities(CT::string *XMLDoc,std::vector<WMSLayer*> *myWMSLayerList);
    int getWCS_1_0_0_DescribeCoverage(CT::string *XMLDoc,std::vector<WMSLayer*> *myWMSLayerList);
    int getStylesForLayer(WMSLayer * myWMSLayer);
    CServerParams *srvParam;
  public:
    int OGCGetCapabilities(CServerParams *srvParam,CT::string *XMLDocument);
    int WCSDescribeCoverage(CServerParams *srvParam,CT::string *XMLDocument);
    int replace(const char *keyword,const char*replace,char*buf);
    int replace_one(const char *keyword,const char*replace,char*buf);
    CT::string *TimePositions;
    
    CXMLGen(){
      TimePositions=NULL;
    }
    ~CXMLGen(){
      if(TimePositions!=NULL)delete[] TimePositions;
      TimePositions=NULL;
    }
};

class CFile{
  private:
  size_t getFileSize(const char *pszFileName){
    FILE *fp=fopen(pszFileName, "r");
    if (fp == NULL) {
      char szTemp[MAX_STR_LEN+1];
      snprintf(szTemp,MAX_STR_LEN,"File not found:[%s]",pszFileName);
      error(szTemp);
      return 0;
    }
    fseek( fp, 0L, SEEK_END );
    long endPos = ftell( fp);
    fclose(fp);
    return endPos;
  }

  int openFileToBuf(const char *pszFileName,char *buf,size_t length){
    FILE *fp=fopen(pszFileName, "r");
    if (fp == NULL) {
      char szTemp[MAX_STR_LEN+1];
      snprintf(szTemp,MAX_STR_LEN,"File not found:[%s]",pszFileName);
      error(szTemp);
      return 1;
    }
    size_t result=fread(buf,1,length,fp);
    if(result!=length){
      //CDBError("openFileToBuf: Number of bytes read != requested");
      return 1;
    }
    fclose(fp);
    return 0;
  }
  public:
    char *data;
    size_t size;
   void error(const char * msg){
      char szDebug[MAX_STR_LEN+1];
      snprintf(szDebug,MAX_STR_LEN,"CFile: %s",msg);
      printerror(szDebug);
    }
    CFile(){
      data=NULL;
    }
    ~CFile(){
      if(data!=NULL)delete[] data;
    }
    int open(const char *pszFileName){
      size = getFileSize(pszFileName);

      if(size==0){
        data = new char[size+1];
        data[size]='\0';
        return 1;
      }
      if(data!=NULL)delete[] data;
      data = new char[size+1];
      int status = openFileToBuf(pszFileName,data,size);
      data[size]='\0';
      return status;
    }
    int open(const char * path,const char *pszFileName){
      char szTemp[MAX_STR_LEN+1];
      snprintf(szTemp,MAX_STR_LEN,"%s/%s",path,pszFileName);
      return open(szTemp);
    }
};

#endif
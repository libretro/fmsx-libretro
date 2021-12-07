/** EMULib Emulation Library *********************************/
/**                                                         **/
/**                          MCF.c                          **/
/**                                                         **/
/** This file contains support for the .MCF cheat file      **/
/** format. See MCF.h for declarations.                     **/
/**                                                         **/
/** Copyright (C) Marat Fayzullin 2017                      **/
/**     You are not allowed to distribute this software     **/
/**     commercially. Please, notify me, if you make any    **/
/**     changes to this file.                               **/
/*************************************************************/
#include "MCF.h"

#include <string.h>

#include <streams/file_stream_transforms.h>

/** LoadFileMCF() ********************************************/
/** Load cheats from .MCF file. Returns number of loaded    **/
/** cheat entries or 0 on failure.                          **/
/*************************************************************/
int LoadFileMCF(const char *Name,MCFEntry *Cheats,int MaxCheats)
{
  char Buf[256],Note[256];
  unsigned int Arg0,Addr,Data,Arg3;
  int J;
  RFILE *F;

  /* Open .MCF text file with cheats */
  F = rfopen(Name,"rb");
  if(!F) return(0);

  /* Load cheats from file */
  for(J=0;!rfeof(F)&&(J<MaxCheats);)
    if(rfgets(Buf,sizeof(Buf),F) && (sscanf(Buf,"%u,%u,%u,%u,%255s",&Arg0,&Addr,&Data,&Arg3,Note)==5))
    {
      Cheats[J].Addr = Addr;
      Cheats[J].Data = Data;
      Cheats[J].Size = Data>0xFFFF? 4:Data>0xFF? 2:1;
      strncpy(Cheats[J].Note,Note,sizeof(Cheats[J].Note));
      Cheats[J].Note[sizeof(Cheats[J].Note)-1] = '\0';
      ++J;
    }

  /* Done with the file */
  rfclose(F);

  /* Done */
  return(J);
}

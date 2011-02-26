/**
 * Mupen64 - dma.c
 * Copyright (C) 2002 Hacktarux
 *
 * Mupen64 homepage: http://mupen64.emulation64.com
 * email address: hacktarux@yahoo.fr
 *
 * If you want to contribute to the project please contact
 * me first (maybe someone is already making what you are
 * planning to do).
 *
 *
 * This program is free software; you can redistribute it and/
 * or modify it under the terms of the GNU General Public Li-
 * cence as published by the Free Software Foundation; either
 * version 2 of the Licence, or any later version.
 *
 * This program is distributed in the hope that it will be use-
 * ful, but WITHOUT ANY WARRANTY; without even the implied war-
 * ranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public Licence for more details.
 *
 * You should have received a copy of the GNU General Public
 * Licence along with this program; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 * USA.
 *
**/

#include <stdio.h>
#include <malloc.h>
#include <ppu-types.h>
#include "dma.h"
#include "memory.h"
#include "../main/rom.h"
#include "../main/guifuncs.h"
#include "../r4300/r4300.h"
#include "../r4300/interupt.h"
#include "../r4300/macros.h"
#include "../fileBrowser/fileBrowser.h"
#include "../r4300/Invalid_Code.h"
#include "../main/ROM-Cache.h"
#include "pif.h"
#include "flashram.h"
#include "Saves.h"

#ifdef USE_EXPANSION
	#define MEMMASK 0x7FFFFF
#else
	#define MEMMASK 0x3FFFFF
#endif

static unsigned char sram[0x8000] __attribute__((aligned(32)));

BOOL sramWritten = FALSE;

int loadSram(fileBrowser_file* savepath){
	int i, result = 0;
	fileBrowser_file saveFile;
	memcpy(&saveFile, savepath, sizeof(fileBrowser_file));
	memset(&saveFile.name[0],0,FILE_BROWSER_MAX_PATH_LEN);
	sprintf((char*)saveFile.name,"%s/%s%s.sra",savepath->name,ROM_SETTINGS.goodname,saveregionstr());

	if(saveFile_readFile(&saveFile, &i, 4) == 4){ //file exists
		saveFile.offset = 0;
		if(saveFile_readFile(&saveFile, sram, 0x8000)!=0x8000) {  //error reading file
  		for (i=0; i<0x8000; i++) sram[i] = 0;
  		sramWritten = FALSE;
  		return -1;
		}
  	result = 1;
  	sramWritten = 1;
		return result;  //file read ok
	} else for (i=0; i<0x8000; i++) sram[i] = 0;  //file doesn't exist

	sramWritten = FALSE;

	return result;    //no file

}

int saveSram(fileBrowser_file* savepath){
  if(!sramWritten) return 0;
	fileBrowser_file saveFile;
	memcpy(&saveFile, savepath, sizeof(fileBrowser_file));
	memset(&saveFile.name[0],0,FILE_BROWSER_MAX_PATH_LEN);
	sprintf((char*)saveFile.name,"%s/%s%s.sra",savepath->name,ROM_SETTINGS.goodname,saveregionstr());

	if(saveFile_writeFile(&saveFile, sram, 0x8000)!=0x8000)
	  return -1;

	return 1;
}

void dma_pi_read()
{
   int i;

   if (pi_register.pi_cart_addr_reg >= 0x08000000 &&
       pi_register.pi_cart_addr_reg < 0x08010000)
     {
	if (use_flashram != 1)
	  {

	     sramWritten = TRUE;

	     for (i=0; i<(pi_register.pi_rd_len_reg & 0xFFFFFF)+1; i++)
	       sram[((pi_register.pi_cart_addr_reg-0x08000000)+i)^S8]=
	       ((unsigned char*)rdram)[(pi_register.pi_dram_addr_reg+i)^S8];

	     use_flashram = -1;
	  }
	else
	  dma_write_flashram();
     }
 //  else
 //    printf("unknown dma read\n");

   pi_register.read_pi_status_reg |= 1;
   update_count();
   add_interupt_event(PI_INT, 0x1000/*pi_register.pi_rd_len_reg*/);
}

void dma_pi_write()
{
   u32 longueur;
   int i;

   if (pi_register.pi_cart_addr_reg < 0x10000000)
     {
	if (pi_register.pi_cart_addr_reg >= 0x08000000 &&
	    pi_register.pi_cart_addr_reg < 0x08010000)
	  {
	     if (use_flashram != 1)
	       {

		  for (i=0; i<(pi_register.pi_wr_len_reg & 0xFFFFFF)+1; i++)
		    ((unsigned char*)rdram)[(pi_register.pi_dram_addr_reg+i)^S8]=
		    sram[(((pi_register.pi_cart_addr_reg-0x08000000)&0xFFFF)+i)^S8];
		  use_flashram = -1;
	       }
	     else
	       dma_read_flashram();
	  }
	else if (pi_register.pi_cart_addr_reg >= 0x06000000 &&
		 pi_register.pi_cart_addr_reg < 0x08000000)
	  {
	  }
//	else
//	  printf("unknown dma write:%x\n", (int)pi_register.pi_cart_addr_reg);

	pi_register.read_pi_status_reg |= 1;
	update_count();
	add_interupt_event(PI_INT, /*pi_register.pi_wr_len_reg*/0x1000);

	return;
     }

   if (pi_register.pi_cart_addr_reg >= 0x1fc00000) // for paper mario
     {
	pi_register.read_pi_status_reg |= 1;
	update_count();
	add_interupt_event(PI_INT, 0x1000);
	return;
     }

   longueur = (pi_register.pi_wr_len_reg & 0xFFFFFF)+1;
   i = (pi_register.pi_cart_addr_reg-0x10000000)&0x3FFFFFF;
   longueur = (i + longueur) > rom_length ?
     (rom_length - i) : longueur;
   longueur = (pi_register.pi_dram_addr_reg + longueur) > MEMMASK ?
     (MEMMASK - pi_register.pi_dram_addr_reg) : longueur;

   if(i>rom_length || pi_register.pi_dram_addr_reg > MEMMASK)
     {
	pi_register.read_pi_status_reg |= 3;
	update_count();
	add_interupt_event(PI_INT, longueur/8);
	return;
     }


     	/*printf("DMA transfer from cart address: %08x\n"
     	       "To RDRAM address: %08x of length %u\n",
     	       ((pi_register.pi_cart_addr_reg-0x10000000)&0x3FFFFFF)^S8,
     	       ((unsigned int)(pi_register.pi_dram_addr_reg)^S8),
     	       longueur);*/
	ROMCache_read((u32*)((char*)rdram + ((u32)(pi_register.pi_dram_addr_reg)^S8)),
	              (((pi_register.pi_cart_addr_reg-0x10000000)&0x3FFFFFF))^S8, longueur);
     

   if ((debug_count+Count) < 0x100000)
     {
	switch(r4300.cic_chip)
	  {
	   case 1:
	   case 2:
	   case 3:
	   case 6:
#ifdef USE_EXPANSION
	     rdram[0x318/4] = 0x800000;
#else
	     rdram[0x318/4] = 0x400000;
#endif
	     break;
	   case 5:
#ifdef USE_EXPANSION
	     rdram[0x3F0/4] = 0x800000;
#else
	     rdram[0x3F0/4] = 0x400000;
#endif
	     break;
	  }
	  /* DK64 Fix */
	/*if(strncmp(ROM_HEADER->nom, "DONKEY KONG 64", 14) == 0)
		rdram[0x2FE1C0/4] = 0xAD170014;*/
     }

   pi_register.read_pi_status_reg |= 3;
   update_count();
   add_interupt_event(PI_INT, longueur/8);
   return;
}

void dma_sp_write()
{
   int i;
   if ((sp_register.sp_mem_addr_reg & 0x1000) > 0)
     {
	for (i=0; i<((sp_register.sp_rd_len_reg & 0xFFF)+1); i++)
	  ((unsigned char *)(SP_IMEM))[((sp_register.sp_mem_addr_reg & 0xFFF)+i)^S8]=
	  ((unsigned char *)(rdram))[((sp_register.sp_dram_addr_reg & 0xFFFFFF)+i)^S8];
     }
   else
     {
	for (i=0; i<((sp_register.sp_rd_len_reg & 0xFFF)+1); i++)
	  ((unsigned char *)(SP_DMEM))[((sp_register.sp_mem_addr_reg & 0xFFF)+i)^S8]=
	  ((unsigned char *)(rdram))[((sp_register.sp_dram_addr_reg & 0xFFFFFF)+i)^S8];
     }
}

void dma_sp_read()
{
   int i;
   if ((sp_register.sp_mem_addr_reg & 0x1000) > 0)
     {
	for (i=0; i<((sp_register.sp_wr_len_reg & 0xFFF)+1); i++)
	  ((unsigned char *)(rdram))[((sp_register.sp_dram_addr_reg & 0xFFFFFF)+i)^S8]=
	  ((unsigned char *)(SP_IMEM))[((sp_register.sp_mem_addr_reg & 0xFFF)+i)^S8];
     }
   else
     {
	for (i=0; i<((sp_register.sp_wr_len_reg & 0xFFF)+1); i++)
	  ((unsigned char *)(rdram))[((sp_register.sp_dram_addr_reg & 0xFFFFFF)+i)^S8]=
	  ((unsigned char *)(SP_DMEM))[((sp_register.sp_mem_addr_reg & 0xFFF)+i)^S8];
     }
}

void dma_si_write()
{
   int i;
   if (si_register.si_pif_addr_wr64b != 0x1FC007C0)
     {
//	printf("unknown SI use\n");
	r4300.stop=1;
     }
   for (i=0; i<(64/4); i++)
     PIF_RAM[i] = sl(rdram[si_register.si_dram_addr/4+i]);
   update_pif_write();
   update_count();
   add_interupt_event(SI_INT, /*0x100*/0x900);
}

void dma_si_read()
{
   int i;
   if (si_register.si_pif_addr_rd64b != 0x1FC007C0)
     {
//	printf("unknown SI use\n");
	r4300.stop=1;
     }
   update_pif_read();
   for (i=0; i<(64/4); i++)
     rdram[si_register.si_dram_addr/4+i] = sl(PIF_RAM[i]);
   update_count();
   add_interupt_event(SI_INT, /*0x100*/0x900);
}

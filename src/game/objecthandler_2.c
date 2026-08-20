#include <ultra64.h>
#include <stdio.h>
#include <stdlib.h>
#include "chrobjdata.h"
#include "image.h"
#include "math_asinfacosf.h"
#include "math_ceil.h"
#include "math_floor.h"
#include "math_unk_05A9E0.h"
#include "model.h"
#include "ob.h"
#include "objecthandler.h"
#include "quaternion.h"
#include "tex.h"

#include "port.h"


/***
 * Perfect Dark:
 * void modeldef0f1a7560(struct modeldef *modeldef, u16 filenum, u32 arg2, struct modeldef *modeldef2, struct texpool *texpool, bool arg5)
 * 
 * NTSC address 0x7F0762E0.
*/

/* On 64-bit, ->Switches is a relocated native pointer array, not the file
 * base; these aliases keep the N64 token stream identical while routing
 * the PC build through the real blob base (filedata, set above). */
#define OBJ_FILE_BASE_U8  ((u8 *)(uintptr_t) filedata)
#define OBJ_FILE_BASE_S32 (filedata)

void sub_GAME_7F0762E0(ModelFileHeader *objheader, u8 *name, u8 *dst, struct texpool *buffer)
{
    ModelNode *node;
    s32 romremaining;
    Gfx *gdl;
    s32 pcremaining;
    u32 replacementgdl;
    ModelNode *curnode;
    Gfx *curgdl;
    s32 delta;
    s32 filedata;
    s32 filenum;

    /* On N64/PC32 the switch table sits at the start of the file, so
     * ->Switches doubles as the file base. The 64-bit build relocates the
     * switch table (4-byte slots can't hold native pointers), so the base
     * comes from the port's registry instead. */
    filedata = (s32) (uintptr_t) portModelFileBase(objheader);
    filenum = fileGetIndex((char *) name);

    romremaining = get_rom_remaining_buffer_for_index(filenum);
    pcremaining = get_pc_remaining_buffer_for_index(filenum);
    node = 0;
    modelIterateDisplayLists(objheader, &node, &gdl);

    if (getenv("PORT_LOAD_TRACE") != NULL) {
        fprintf(stderr, "port/rewrite: %s first-gdl=%p node=%p\n", name, (void *)gdl, (void *)node);
    }

    if (gdl != 0)
    {
        name = (u8 *) ((pcremaining - ((s32) (OBJ_FILE_BASE_U8 + (((u32) gdl) & 0x00ffffff)))) + ((s32) filedata));
        
        /* The signed lvalue cast is required for the compiler to choose the target registers. */
        replacementgdl = (u32)*(s32 *)&gdl;
        
        delta = ((s32) ((romremaining + filedata) - (s32) name)) - ((s32) (OBJ_FILE_BASE_U8 + (((u32) gdl) & 0x00ffffff)));
        
        texCopyGdls((Gfx *) (OBJ_FILE_BASE_U8 + (((u32) gdl) & 0x00ffffff)), (Gfx *) ((romremaining + filedata) - (s32) name), (s32) name);

        texLoadFromModelFileHeader(objheader, buffer);

        if (node != 0)
        {
            do
            {
                curnode = node;
                curgdl = gdl;
                modelIterateDisplayLists(objheader, &node, &gdl);
                
                if (gdl != 0)
                {
                    name = (u8 *) (((s32) gdl) - ((s32) curgdl));
                }
                else
                {
                    name = (u8 *) ((((s32) (filedata + pcremaining)) - OBJ_FILE_BASE_S32) - (((u32) curgdl) & 0x00ffffff));
                }
                
                modelNodeReplaceGdl((u32) objheader, curnode, curgdl, (Gfx *) replacementgdl);
                
                replacementgdl += texLoadFromGdl( (Gfx *) ((OBJ_FILE_BASE_U8 + (((u32) curgdl) & 0x00ffffff)) + delta), (s32) name, (Gfx *) (OBJ_FILE_BASE_U8 + (replacementgdl & 0x00ffffff)), buffer);
            } 
            while (node != 0);
        }

        name = (u8 *) (((s32) (OBJ_FILE_BASE_U8 + (replacementgdl & 0x00ffffff))) - filedata);

        fileSetSize(filenum, (u8 *) filedata, (((s32) name + 0xf) & (~0xf)), dst == 0);
    }
}


/***
 * NTSC addres 0x7F0764A4.
*/
void load_object_fill_header(struct ModelFileHeader *objheader, u8 *name, u8* dst, s32 size, struct texpool * buffer)
{
    void *filedata;

    if (dst != 0)
    {
        filedata = _fileNameLoadToAddr(name, 0, dst, size);
    }
    else
    {
        filedata = _fileNameLoadToBank(name, 0, 0x100, 4);
    }
    
    objheader->Switches = (struct ModelNode **)filedata;
    
    // hmmmmmmmmmmmm
    objheader->Textures = (struct ModelFileTextures *)&((s32*)filedata)[objheader->numSwitches];
    
    objheader->RootNode = (struct ModelNode *)&objheader->Textures[objheader->numtextures];

    /* PORT_PREPROCESS: the blob is big-endian on ROM */
    portPreprocessModelFile(objheader, filedata, 0x5000000, (u32)size);
    if (getenv("PORT_LOAD_TRACE") != NULL) {
        fprintf(stderr, "port/load: model %s -> %p (size %d)\n", name, filedata, size);
    }

    if (!IS_64_BIT) {
        sub_GAME_7F075A90(objheader, 0x5000000, filedata);
    }
    /* 64-bit: portPreprocessModelFile already rebuilt the node tree in
     * native layout with fully promoted pointers (the in-place promote
     * would read 4-byte blob slots through 8-byte struct fields). */
    sub_GAME_7F0762E0(objheader, name, dst, buffer);
}




void fileLoad(struct ModelFileHeader *header,char *name)
{
   load_object_fill_header(header,name,0,0,0);
   return;
}


void load_object_into_memory_unused_maybe(struct ModelFileHeader *header,int *recallstring,int *targetloc,int sizeleft)
{
   load_object_fill_header(header,recallstring,targetloc,sizeleft,0);
   return;
}






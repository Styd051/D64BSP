//------------------------------------------------------------------------
// BLOCKMAP : Generate the blockmap
//------------------------------------------------------------------------
//
//  GL-Friendly Node Builder (C) 2000-2007 Andrew Apted
//
//  Based on 'BSP 2.3' by Colin Reed, Lee Killough and others.
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 2
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//------------------------------------------------------------------------

#include "system.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <math.h>
#include <limits.h>
#include <assert.h>

#include "blockmap.h"
#include "level.h"
#include "node.h"
#include "seg.h"
#include "structs.h"
#include "util.h"
#include "wad.h"


#define DEBUG_BLOCKMAP  0


static int block_x, block_y;
static int block_w, block_h;
static int block_count;

static int block_mid_x = 0;
static int block_mid_y = 0;

static uint16_g ** block_lines;

static uint16_g *block_ptrs;
static uint16_g *block_dups;

static int block_compression;
static int block_shared;

#define DUMMY_DUP  0xFFFF

// the three engines read the offsets of the lists and the numbers of the
// lines as signed numbers
#define BLOCK_MAX_OFFSET  32767
#define BLOCK_MAX_LINES   32768


//
// GetBlockmapBounds
//
void GetBlockmapBounds(int *x, int *y, int *w, int *h)
{
  *x = block_x; *y = block_y;
  *w = block_w; *h = block_h;
}

//
// CheckLinedefInsideBox
//
int CheckLinedefInsideBox(int xmin, int ymin, int xmax, int ymax,
    int x1, int y1, int x2, int y2)
{
  int count = 2;
  int tmp;

  for (;;)
  {
    if (y1 > ymax)
    {
      if (y2 > ymax)
        return FALSE;
        
      x1 = x1 + (int) ((x2-x1) * (double)(ymax-y1) / (double)(y2-y1));
      y1 = ymax;
      
      count = 2;
      continue;
    }

    if (y1 < ymin)
    {
      if (y2 < ymin)
        return FALSE;
      
      x1 = x1 + (int) ((x2-x1) * (double)(ymin-y1) / (double)(y2-y1));
      y1 = ymin;
      
      count = 2;
      continue;
    }

    if (x1 > xmax)
    {
      if (x2 > xmax)
        return FALSE;
        
      y1 = y1 + (int) ((y2-y1) * (double)(xmax-x1) / (double)(x2-x1));
      x1 = xmax;

      count = 2;
      continue;
    }

    if (x1 < xmin)
    {
      if (x2 < xmin)
        return FALSE;
        
      y1 = y1 + (int) ((y2-y1) * (double)(xmin-x1) / (double)(x2-x1));
      x1 = xmin;

      count = 2;
      continue;
    }

    count--;

    if (count == 0)
      break;

    /* swap end points */
    tmp=x1;  x1=x2;  x2=tmp;
    tmp=y1;  y1=y2;  y2=tmp;
  }

  /* linedef touches block */
  return TRUE;
}


/* ----- create blockmap ------------------------------------ */

#define BK_NUM    0
#define BK_MAX    1
#define BK_XOR    2
#define BK_FIRST  3

#define BK_QUANTUM  32

static void BlockAdd(int blk_num, int line_index)
{
  uint16_g *cur = block_lines[blk_num];

# if DEBUG_BLOCKMAP
  PrintDebug("Block %d has line %d\n", blk_num, line_index);
# endif

  if (blk_num < 0 || blk_num >= block_count)
    InternalError("BlockAdd: bad block number %d", blk_num);
    
  if (! cur)
  {
    // create empty block
    block_lines[blk_num] = cur = UtilCalloc(BK_QUANTUM * 
        sizeof(uint16_g));
    cur[BK_NUM] = 0;
    cur[BK_MAX] = BK_QUANTUM;
    cur[BK_XOR] = 0x1234;
  }

  if (BK_FIRST + cur[BK_NUM] == cur[BK_MAX])
  {
    // no more room, so allocate some more...
    cur[BK_MAX] += BK_QUANTUM;

    block_lines[blk_num] = cur = UtilRealloc(cur, cur[BK_MAX] * 
        sizeof(uint16_g));
  }

  // compute new checksum
  cur[BK_XOR] = ((cur[BK_XOR] << 4) | (cur[BK_XOR] >> 12)) ^ line_index;

  cur[BK_FIRST + cur[BK_NUM]] = UINT16(line_index);
  cur[BK_NUM]++;
}

//
// LineInBlock
//
// The test of the tool of Midway, on the exact coordinates.  The bounds of
// the line must reach the block, its high edges being left out, and the four
// corners of the block must not be strictly on the same side of the line.
//
static int LineInBlock(const linedef_t *L, float_g xl, float_g yl)
{
  float_g x1 = L->start->x;
  float_g y1 = L->start->y;
  float_g x2 = L->end->x;
  float_g y2 = L->end->y;

  float_g xh = xl + 128;
  float_g yh = yl + 128;

  float_g dx = x2 - x1;
  float_g dy = y2 - y1;

  int ahead = 0;
  int behind = 0;
  int i;

  if (MIN(x1,x2) >= xh || MAX(x1,x2) < xl ||
      MIN(y1,y2) >= yh || MAX(y1,y2) < yl)
    return FALSE;

  for (i=0; i < 4; i++)
  {
    float_g cx = (i & 1) ? xh : xl;
    float_g cy = (i & 2) ? yh : yl;

    float_g side = (cx - x1) * dy - (cy - y1) * dx;

    if (side > 0) ahead++;
    if (side < 0) behind++;
  }

  return (ahead < 4 && behind < 4);
}

static void BlockAddLine(linedef_t *L)
{
  float_g x1 = L->start->x;
  float_g y1 = L->start->y;
  float_g x2 = L->end->x;
  float_g y2 = L->end->y;

  int bx1 = (int) floor((MIN(x1,x2) - block_x) / 128);
  int by1 = (int) floor((MIN(y1,y2) - block_y) / 128);
  int bx2 = (int) floor((MAX(x1,x2) - block_x) / 128);
  int by2 = (int) floor((MAX(y1,y2) - block_y) / 128);

  int bx, by;
  int line_index = L->index;

# if DEBUG_BLOCKMAP
  PrintDebug("BlockAddLine: %d (%1.1f,%1.1f) -> (%1.1f,%1.1f)\n", line_index,
      x1, y1, x2, y2);
# endif

  // handle truncated blockmaps
  if (bx1 < 0) bx1 = 0;
  if (by1 < 0) by1 = 0;
  if (bx2 >= block_w) bx2 = block_w - 1;
  if (by2 >= block_h) by2 = block_h - 1;

  if (bx2 < bx1 || by2 < by1)
    return;

  for (by=by1; by <= by2; by++)
  for (bx=bx1; bx <= bx2; bx++)
  {
    if (LineInBlock(L, block_x + bx * 128, block_y + by * 128))
    {
      BlockAdd(by * block_w + bx, line_index);
    }
  }
}

static void CreateBlockmap(void)
{
  int i;

  block_lines = UtilCalloc(block_count * sizeof(uint16_g *));

  DisplayTicker();

  for (i=0; i < num_linedefs; i++)
  {
    linedef_t *L = LookupLinedef(i);

    // ignore zero-length lines
    if (L->zero_len)
      continue;

    BlockAddLine(L);
  }
}


//
// LayoutBlockmap
//
// One list for each block, in the order of the blocks, as in the maps of
// Midway.  Returns FALSE when a list would start beyond the last offset.
//
static int LayoutBlockmap(void)
{
  int i;
  int cur_offset = 4 + block_count;

  block_ptrs = UtilCalloc(block_count * sizeof(uint16_g));
  block_dups = UtilCalloc(block_count * sizeof(uint16_g));

  for (i=0; i < block_count; i++)
  {
    if (cur_offset > BLOCK_MAX_OFFSET)
      return FALSE;

    block_ptrs[i] = cur_offset;
    block_dups[i] = i;

    cur_offset += 1 + (block_lines[i] ? block_lines[i][BK_NUM] : 0);
  }

  return TRUE;
}


static int BlockCompare(const void *p1, const void *p2)
{
  int blk_num1 = ((const uint16_g *) p1)[0];
  int blk_num2 = ((const uint16_g *) p2)[0];

  const uint16_g *A = block_lines[blk_num1];
  const uint16_g *B = block_lines[blk_num2];

  if (A == B)
    return 0;

  if (A == NULL) return -1;
  if (B == NULL) return +1;

  if (A[BK_NUM] != B[BK_NUM])
  {
    return A[BK_NUM] - B[BK_NUM];
  }
 
  if (A[BK_XOR] != B[BK_XOR])
  {
    return A[BK_XOR] - B[BK_XOR];
  }
 
  return memcmp(A+BK_FIRST, B+BK_FIRST, A[BK_NUM] * sizeof(uint16_g));
}

static void CompressBlockmap(void)
{
  int i;
  int cur_offset;
  int last_offset;
  int dup_count=0;

  int orig_size, new_size;

  DisplayTicker();

  // sort duplicate-detecting array.  After the sort, all duplicates
  // will be next to each other.  The duplicate array gives the order
  // of the blocklists in the BLOCKMAP lump.
  
  for (i=0; i < block_count; i++)
    block_dups[i] = i;

  qsort(block_dups, block_count, sizeof(uint16_g), BlockCompare);

  // scan duplicate array and build up offset array

  cur_offset = 4 + block_count + 1;
  last_offset = 4 + block_count;

  orig_size = 4 + block_count;
  new_size  = cur_offset;

  DisplayTicker();

  for (i=0; i < block_count; i++)
  {
    int blk_num = block_dups[i];
    int count;

    // empty block ?
    if (block_lines[blk_num] == NULL)
    {
      block_ptrs[blk_num] = 4 + block_count;
      block_dups[i] = DUMMY_DUP;

      orig_size += 1;
      continue;
    }

    count = 1 + block_lines[blk_num][BK_NUM];

    // duplicate ?  Only the very last one of a sequence of duplicates
    // will update the current offset value.

    if (i+1 < block_count && 
        BlockCompare(block_dups + i, block_dups + i+1) == 0)
    {
      block_ptrs[blk_num] = cur_offset;
      block_dups[i] = DUMMY_DUP;

      // free the memory of the duplicated block
      UtilFree(block_lines[blk_num]);
      block_lines[blk_num] = NULL;
      
      dup_count++;

      orig_size += count;
      continue;
    }

    // OK, this block is either the last of a series of duplicates, or
    // just a singleton.

    block_ptrs[blk_num] = cur_offset;
    last_offset = cur_offset;

    cur_offset += count;

    orig_size += count;
    new_size  += count;
  }

  if (last_offset > BLOCK_MAX_OFFSET)
  {
    FatalError("The BLOCKMAP of %s is too large: a list starts at offset %d, "
        "the engines read %d at most", GetLevelName(), last_offset,
        BLOCK_MAX_OFFSET);
  }

# if DEBUG_BLOCKMAP
  PrintDebug("Blockmap: Last ptr = %d  duplicates = %d\n", 
      cur_offset, dup_count);
# endif

  block_compression = (orig_size - new_size) * 100 / orig_size;

  // there's a tiny chance of new_size > orig_size
  if (block_compression < 0)
    block_compression = 0;
}


static void WriteBlockmap(void)
{
  int i;
  
  raw_blockmap_header_t header;

  lump_t *lump = CreateLevelLump("BLOCKMAP");

  uint16_g m_neg1 = 0xFFFF;

  // fill in header
  header.x_origin = UINT16(block_x);
  header.y_origin = UINT16(block_y);
  header.x_blocks = UINT16(block_w);
  header.y_blocks = UINT16(block_h);
  
  AppendLevelLump(lump, &header, sizeof(header));

  // handle pointers
  for (i=0; i < block_count; i++)
  {
    uint16_g ptr = UINT16(block_ptrs[i]);

    if (ptr == 0)
      InternalError("WriteBlockmap: offset %d not set.", i);

    AppendLevelLump(lump, &ptr, sizeof(uint16_g));
  }

  // add the null block which _all_ empty blocks will use
  if (block_shared)
    AppendLevelLump(lump, &m_neg1, sizeof(uint16_g));

  // handle each block list
  for (i=0; i < block_count; i++)
  {
    int blk_num = block_dups[i];
    uint16_g *blk;

    // ignore duplicate or empty blocks
    if (blk_num == DUMMY_DUP)
      continue;

    blk = block_lines[blk_num];

    if (blk == NULL && block_shared)
      InternalError("WriteBlockmap: block %d is NULL !", i);

    if (blk != NULL)
      AppendLevelLump(lump, blk + BK_FIRST, blk[BK_NUM] * sizeof(uint16_g));

    AppendLevelLump(lump, &m_neg1, sizeof(uint16_g));
  }
}


static void FreeBlockmap(void)
{
  int i;

  for (i=0; i < block_count; i++)
  {
    if (block_lines[i])
      UtilFree(block_lines[i]);
  }

  UtilFree(block_lines);
  UtilFree(block_ptrs);
  UtilFree(block_dups);
}


/* ----- top level funcs ------------------------------------ */

static void FindBlockmapLimits(bbox_t *bbox)
{
  int i;

  int mid_x = 0;
  int mid_y = 0;

  bbox->minx = bbox->miny = SHRT_MAX;
  bbox->maxx = bbox->maxy = SHRT_MIN;

  for (i=0; i < num_linedefs; i++)
  {
    linedef_t *L = LookupLinedef(i);

    if (! L->zero_len)
    {
      float_g x1 = L->start->x;
      float_g y1 = L->start->y;
      float_g x2 = L->end->x;
      float_g y2 = L->end->y;

      int lx = (int)floor(MIN(x1, x2));
      int ly = (int)floor(MIN(y1, y2));
      int hx = (int)ceil(MAX(x1, x2));
      int hy = (int)ceil(MAX(y1, y2));

      if (lx < bbox->minx) bbox->minx = lx;
      if (ly < bbox->miny) bbox->miny = ly;
      if (hx > bbox->maxx) bbox->maxx = hx;
      if (hy > bbox->maxy) bbox->maxy = hy;

      // compute middle of cluster (roughly, so we don't overflow)
      mid_x += (lx + hx) / 32;
      mid_y += (ly + hy) / 32;
    }
  }

  if (num_linedefs > 0)
  {
    block_mid_x = (mid_x / num_linedefs) * 16;
    block_mid_y = (mid_y / num_linedefs) * 16;
  }

# if DEBUG_BLOCKMAP
  PrintDebug("Blockmap lines centered at (%d,%d)\n", block_mid_x, block_mid_y);
# endif
}

//
// UseMidwayBounds
//
// The BLOCKMAP of the maps of Midway starts 8 units before their lowest
// coordinates, and goes 8 units beyond their highest ones.  The values of
// InitBlockmap stay until the nodes are built: the superblocks are made from
// them, and they decide the order of the segs.
//
static void UseMidwayBounds(void)
{
  int i;

  float_g min_x = 65536, min_y = 65536;
  float_g max_x = -65536, max_y = -65536;

  for (i=0; i < num_linedefs; i++)
  {
    linedef_t *L = LookupLinedef(i);

    if (L->zero_len)
      continue;

    min_x = MIN(min_x, MIN(L->start->x, L->end->x));
    min_y = MIN(min_y, MIN(L->start->y, L->end->y));
    max_x = MAX(max_x, MAX(L->start->x, L->end->x));
    max_y = MAX(max_y, MAX(L->start->y, L->end->y));
  }

  block_x = (int) floor(min_x) - 8;
  block_y = (int) floor(min_y) - 8;

  block_w = (int) floor((max_x - min_x + 16) / 128) + 1;
  block_h = (int) floor((max_y - min_y + 16) / 128) + 1;
  block_count = block_w * block_h;
}

//
// TruncateBlockmap
//
static void TruncateBlockmap(void)
{
  while (block_w * block_h > cur_info->block_limit)
  {
    block_w -= block_w / 8;
    block_h -= block_h / 8;
  }

  block_count = block_w * block_h;

  PrintMiniWarn("Blockmap TOO LARGE!  Truncated to %dx%d blocks\n",
      block_w, block_h);

  MarkSoftFailure(LIMIT_BMAP_TRUNC);

  /* center the truncated blockmap */
  block_x = block_mid_x - block_w * 64;
  block_y = block_mid_y - block_h * 64;

# if DEBUG_BLOCKMAP
  PrintDebug("New blockmap origin: (%d,%d)\n", block_x, block_y);
# endif
}

//
// InitBlockmap
//
void InitBlockmap(void)
{
  bbox_t map_bbox;

  /* find limits of linedefs, and store as map limits */
  FindBlockmapLimits(&map_bbox);

  PrintVerbose("Map goes from (%d,%d) to (%d,%d)\n",
      map_bbox.minx, map_bbox.miny, map_bbox.maxx, map_bbox.maxy);

  block_x = map_bbox.minx - (map_bbox.minx & 0x7);
  block_y = map_bbox.miny - (map_bbox.miny & 0x7);

  block_w = ((map_bbox.maxx - block_x) / 128) + 1;
  block_h = ((map_bbox.maxy - block_y) / 128) + 1;
  block_count = block_w * block_h;

}

//
// PutBlockmap
//
void PutBlockmap(void)
{
  if (num_linedefs > BLOCK_MAX_LINES)
  {
    FatalError("%s has %d lines: the BLOCKMAP can only hold the %d first ones, "
        "the engines read the higher numbers as negative", GetLevelName(),
        num_linedefs, BLOCK_MAX_LINES);
  }

  UseMidwayBounds();

  // truncate blockmap if too large.  We're limiting the number of
  // blocks to around 16000 (user changeable), this leaves about 48K
  // of shorts for the actual line lists.
 
  if (block_count > cur_info->block_limit)
    TruncateBlockmap();

  // initial phase: create internal blockmap containing the index of
  // all lines in each block.
  
  CreateBlockmap();

  // second phase: a list for each block.  If that does not fit...

  block_compression = 0;
  block_shared = ! LayoutBlockmap();

  // -AJA- ...compress the blockmap.  We do this by sorting
  //       the blocks, which is a typical way to detect duplicates in
  //       a large list.

  if (block_shared)
  {
    PrintMiniWarn("Blockmap too large for a list per block: lists are shared\n");
    CompressBlockmap();
  }
 
  // final phase: write it out in the correct format

  WriteBlockmap();

  PrintVerbose("Completed blockmap, size %dx%d (compression: %d%%)\n",
      block_w, block_h, block_compression);

  FreeBlockmap();
}


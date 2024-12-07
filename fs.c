// ============================================================================
// fs.c - user FileSytem API
// ============================================================================

#include "bfs.h"
#include "fs.h"

// ============================================================================
// Close the file currently open on file descriptor 'fd'.
// ============================================================================
i32 fsClose(i32 fd) { 
  i32 inum = bfsFdToInum(fd);
  bfsDerefOFT(inum);
  return 0; 
}



// ============================================================================
// Create the file called 'fname'.  Overwrite, if it already exsists.
// On success, return its file descriptor.  On failure, EFNF
// ============================================================================
i32 fsCreate(str fname) {
  i32 inum = bfsCreateFile(fname);
  if (inum == EFNF) return EFNF;
  return bfsInumToFd(inum);
}



// ============================================================================
// Format the BFS disk by initializing the SuperBlock, Inodes, Directory and 
// Freelist.  On succes, return 0.  On failure, abort
// ============================================================================
i32 fsFormat() {
  FILE* fp = fopen(BFSDISK, "w+b");
  if (fp == NULL) FATAL(EDISKCREATE);

  i32 ret = bfsInitSuper(fp);               // initialize Super block
  if (ret != 0) { fclose(fp); FATAL(ret); }

  ret = bfsInitInodes(fp);                  // initialize Inodes block
  if (ret != 0) { fclose(fp); FATAL(ret); }

  ret = bfsInitDir(fp);                     // initialize Dir block
  if (ret != 0) { fclose(fp); FATAL(ret); }

  ret = bfsInitFreeList();                  // initialize Freelist
  if (ret != 0) { fclose(fp); FATAL(ret); }

  ret = bfsInitOFT();                  	   // initialize OFT
  if (ret != 0) { fclose(fp); FATAL(ret); }

  fclose(fp);
  return 0;
}


// ============================================================================
// Mount the BFS disk.  It must already exist
// ============================================================================
i32 fsMount() {
  FILE* fp = fopen(BFSDISK, "rb");
  if (fp == NULL) FATAL(ENODISK);           // BFSDISK not found
  fclose(fp);
  return 0;
}



// ============================================================================
// Open the existing file called 'fname'.  On success, return its file 
// descriptor.  On failure, return EFNF
// ============================================================================
i32 fsOpen(str fname) {
  i32 inum = bfsLookupFile(fname);        // lookup 'fname' in Directory
  if (inum == EFNF) return EFNF;
  return bfsInumToFd(inum);
}



// ============================================================================
// Read 'numb' bytes of data from the cursor in the file currently fsOpen'd on
// File Descriptor 'fd' into 'buf'.  On success, return actual number of bytes
// read (may be less than 'numb' if we hit EOF).  On failure, abort
// ============================================================================
i32 fsRead(i32 fd, i32 numb, void* buf) {
  i32 currInum = bfsFdToInum(fd);
  i32 currCursor = bfsTell(fd);

  // find FBNs for the file that is open
  i32 left = currCursor / BYTESPERBLOCK;
  i32 right = (currCursor + numb) / BYTESPERBLOCK;
  i32 fileSize = bfsGetSize(currInum);

  // re-adjust the FBN if reading more than the size of the file
  if (numb + currCursor > fileSize) {
    numb = fileSize - currCursor;
    right = (currCursor + numb) / BYTESPERBLOCK;
  }

  // start reading into a temporary buffer and then into the provided buffer
  i8 tempBuf[BYTESPERBLOCK];
  int offset = 0;

  // if reading from one FBN
  if (left == right) {
    bfsRead(currInum, left, tempBuf);

    // offset in the block to where the cursor is before copying to buf
    i32 startOffset = currCursor % BYTESPERBLOCK; 
    memcpy(buf, tempBuf + startOffset, numb);

    // move the current cursor
    fsSeek(fd, numb, SEEK_CUR);
    return numb;
  }

  // if reading from multiple FBNs
  for (i32 i = left; i <= right; i++) {
    bfsRead(currInum, i, tempBuf);

    i32 startOffset = 0;
    i32 bytesToCopy = BYTESPERBLOCK;

    // if on the first FBN, adjust startOffset to read only from the cursor onwards
    if (i == left) {
      startOffset = currCursor % BYTESPERBLOCK;
      bytesToCopy -= startOffset;
    }

    // if on the last FBN, adjust bytesToCopy to read only the required portion
    if (i == right && (numb % BYTESPERBLOCK) != 0) {
      bytesToCopy = numb % BYTESPERBLOCK;
    }

    // copy from tempBuf into buf
    memcpy(buf + offset, tempBuf + startOffset, bytesToCopy);
    offset += bytesToCopy;
  }

  // move the current cursor
  fsSeek(fd, numb, SEEK_CUR);
  return numb;
}


// ============================================================================
// Move the cursor for the file currently open on File Descriptor 'fd' to the
// byte-offset 'offset'.  'whence' can be any of:
//
//  SEEK_SET : set cursor to 'offset'
//  SEEK_CUR : add 'offset' to the current cursor
//  SEEK_END : add 'offset' to the size of the file
//
// On success, return 0.  On failure, abort
// ============================================================================
i32 fsSeek(i32 fd, i32 offset, i32 whence) {

  if (offset < 0) FATAL(EBADCURS);
 
  i32 inum = bfsFdToInum(fd);
  i32 ofte = bfsFindOFTE(inum);
  
  switch(whence) {
    case SEEK_SET:
      g_oft[ofte].curs = offset;
      break;
    case SEEK_CUR:
      g_oft[ofte].curs += offset;
      break;
    case SEEK_END: {
        i32 end = fsSize(fd);
        g_oft[ofte].curs = end + offset;
        break;
      }
    default:
        FATAL(EBADWHENCE);
  }
  return 0;
}



// ============================================================================
// Return the cursor position for the file open on File Descriptor 'fd'
// ============================================================================
i32 fsTell(i32 fd) {
  return bfsTell(fd);
}



// ============================================================================
// Retrieve the current file size in bytes.  This depends on the highest offset
// written to the file, or the highest offset set with the fsSeek function.  On
// success, return the file size.  On failure, abort
// ============================================================================
i32 fsSize(i32 fd) {
  i32 inum = bfsFdToInum(fd);
  return bfsGetSize(inum);
}



// ============================================================================
// Write 'numb' bytes of data from 'buf' into the file currently fsOpen'd on
// filedescriptor 'fd'.  The write starts at the current file offset for the
// destination file.  On success, return 0.  On failure, abort
// ============================================================================
i32 fsWrite(i32 fd, i32 numb, void* buf) {
  i32 currInum = bfsFdToInum(fd);
  i32 currCursor = bfsTell(fd);

  // find FBNs for the file that is open
  i32 left = currCursor / BYTESPERBLOCK;
  i32 right = (currCursor + numb) / BYTESPERBLOCK;
  i32 fileSize = bfsGetSize(currInum);

  // check to see if writing within file or more than the size of the file
  if (currCursor + numb > fileSize) {
    bfsExtend(currInum, right);
    bfsSetSize(currInum, currCursor + numb);
  }

  // number of blocks we need to write numb bytes
  i8 len = right - left + 1;
  i8 simulatedMem[len * BYTESPERBLOCK];

  // write the first and last FBN into simulated memory space
  // because we dont know how many bytes to overwrite
  bfsRead(currInum, left, simulatedMem);
  if (len > 1) {
    bfsRead(currInum, right, simulatedMem + (len - 1) * BYTESPERBLOCK);
  }

  // fill in the rest of the simulated memory with contents from buf
  memcpy(simulatedMem + currCursor % BYTESPERBLOCK, buf, numb);

  // start writing to memory
  int offset = 0;
  i8 tempBuf[BYTESPERBLOCK];

  for (i32 i = left; i <= right; i++) {
    memcpy(tempBuf, simulatedMem + offset, BYTESPERBLOCK);
    bioWrite(bfsFbnToDbn(currInum, i), tempBuf);
    offset += BYTESPERBLOCK;
  }

  // move the current cursor
  fsSeek(fd, numb, SEEK_CUR);
  return 0;
}

/*
 * Copyright 2017 Julian Harnath <julian.harnath@rwth-aachen.de>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include "ShareNode.h"

#include <storage/Node.h>
#include <NodeMonitor.h>

#include <fs_interface.h>
#include <sys/stat.h>

#include "SambaContext.h"
#include "Volume.h"


using namespace Smb;


ShareNode::ShareNode(const BString& url, size_t nameLength, Volume* volume,
	SambaContext* context)
	:
	Node(url, nameLength, volume, context)
{
}


ShareNode::ShareNode(const ShareNode& prototype, const BString& newURL,
	size_t nameLength)
	:
	Node(prototype, newURL, nameLength)
{
}


ShareNode::~ShareNode()
{
}


status_t
ShareNode::ReadStat(struct stat* destination)
{
	// Prefill some values not filled in by Samba
	destination->st_dev = fVolume->ID();
	destination->st_ino = fID;
		// Samba only fills in when 0
	destination->st_blksize = 4096;
	destination->st_type = 0;

	GlobalSambaLocker sambaLocker(gGlobalSambaLock);
	status_t status= fSambaContext->Stat(fURL, destination);
	if (status != B_OK)
		return status;

	// Mask out the executable bits, libsmbclient maps these
	// to the DOS bits for system/hidden/etc
	destination->st_mode &= ~(S_IXUSR | S_IXGRP | S_IXOTH);
	return B_OK;
}


status_t
ShareNode::WriteStat(const struct stat* source, uint32 statMask)
{
	GlobalSambaLocker sambaLocker(gGlobalSambaLock);
	status_t status;

	if (   (statMask & B_STAT_SIZE) != 0
		|| (statMask & B_STAT_SIZE_INSECURE) != 0) {
		// Samba only gives us ftruncate(), so we need to open the
		// file first
		SMBCFILE* file = NULL;
		status = fSambaContext->Open(fURL, O_WRONLY, &file);
		if (status != B_OK)
			return status;

		status = fSambaContext->FileTruncate(file, source->st_size);
		if (status != B_OK) {
			fSambaContext->Close(file);
			return status;
		}

		status = fSambaContext->Close(file);
		if (status != B_OK)
			return status;
	}

	if ((statMask & B_STAT_MODIFICATION_TIME) != 0) {
		status = fSambaContext->UpdateTime(fURL, source->st_mtim);
		if (status != B_OK)
			return status;
	}

	// Other flags in statMask are not supported by Samba
	// B_STAT_MODE could be implemented with Samba's chmod, but it means
	// something different there: it sets the archive/system/hidden flags

	return B_OK;
}


status_t
ShareNode::Open(int flags, void** outCookie)
{
	GlobalSambaLocker sambaLocker(gGlobalSambaLock);
	SMBCFILE* file = NULL;
	status_t status = fSambaContext->Open(fURL, flags, &file);
	if (status != B_OK)
		return status;

	*outCookie = (void*)file;
	return B_OK;
}


status_t
ShareNode::Close(void* cookie)
{
	GlobalSambaLocker sambaLocker(gGlobalSambaLock);
	return fSambaContext->Close(static_cast<SMBCFILE*>(cookie));
}


status_t
ShareNode::FreeCookie(void*)
{
	return B_OK;
}

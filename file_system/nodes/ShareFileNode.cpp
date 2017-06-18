/*
 * Copyright 2017 Julian Harnath <julian.harnath@rwth-aachen.de>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include "ShareFileNode.h"

#include "SambaContext.h"


using namespace Smb;


ShareFileNode::ShareFileNode(const BString& url, size_t nameLength,
	Volume* volume, SambaContext* context)
	:
	ShareNode(url, nameLength, volume, context)
{
}


ShareFileNode::ShareFileNode(const ShareFileNode& prototype,
	const BString& newURL, size_t nameLength)
	:
	ShareNode(prototype, newURL, nameLength)
{
}


ShareFileNode::~ShareFileNode()
{
}


NodeType
ShareFileNode::Type() const
{
	return kShareFile;
}


// #pragma mark - File-only


status_t
ShareFileNode::Read(void* cookie, off_t offset, void* buffer, size_t* length)
{
	if (offset < 0)
		return B_BAD_VALUE;

	GlobalSambaLocker sambaLocker(gGlobalSambaLock);

	SMBCFILE* const file = static_cast<SMBCFILE*>(cookie);
	status_t status = fSambaContext->Seek(file, offset);
	if (status != B_OK)
		return status;

	status = fSambaContext->Read(file, buffer, length);
	if (status != B_OK)
		return status;

	return B_OK;
}


status_t
ShareFileNode::Write(void* cookie, off_t offset, const void* buffer,
	size_t* length)
{
	if (offset < 0)
		return B_BAD_VALUE;

	GlobalSambaLocker sambaLocker(gGlobalSambaLock);

	SMBCFILE* const file = static_cast<SMBCFILE*>(cookie);
	status_t status = fSambaContext->Seek(file, offset);
	if (status != B_OK)
		return status;

	status = fSambaContext->Write(file, buffer, length);
	if (status != B_OK)
		return status;

	return B_OK;
}


// #pragma mark - Directory-only, just fail


status_t
ShareFileNode::Lookup(const char*, ino_t*)
{
	return B_NOT_A_DIRECTORY;
}


status_t
ShareFileNode::Create(const char*, int, int, void**, ino_t*)
{
	return B_NOT_A_DIRECTORY;
}


status_t
ShareFileNode::Remove(const char*)
{
	return B_NOT_A_DIRECTORY;
}


status_t
ShareFileNode::Rename(const char*, Node*, const char*)
{
	return B_NOT_A_DIRECTORY;
}


status_t
ShareFileNode::OpenDir(void**)
{
	return B_NOT_A_DIRECTORY;
}


status_t
ShareFileNode::CloseDir(void*)
{
	return B_NOT_A_DIRECTORY;
}


status_t
ShareFileNode::ReadDir(void*, struct dirent*, size_t, uint32*)
{
	return B_NOT_A_DIRECTORY;
}


status_t
ShareFileNode::FreeDirCookie(void*)
{
	return B_NOT_A_DIRECTORY;
}


status_t
ShareFileNode::RewindDirCookie(void*)
{
	return B_NOT_A_DIRECTORY;
}


status_t
ShareFileNode::CreateDir(const char*, int)
{
	return B_NOT_A_DIRECTORY;
}


status_t
ShareFileNode::RemoveDir(const char*)
{
	return B_NOT_A_DIRECTORY;
}

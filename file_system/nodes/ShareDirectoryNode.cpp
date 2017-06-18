/*
 * Copyright 2017 Julian Harnath <julian.harnath@rwth-aachen.de>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include "ShareDirectoryNode.h"

#include <fs_interface.h>
#include <private/shared/AutoLocker.h>

#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "SambaContext.h"
#include "ShareFileNode.h"
#include "Volume.h"


//#define TRACE_NODE
#ifdef TRACE_NODE
#	define TRACE(text, ...) \
	fprintf(stderr, "SMB-FS [ShareDirectoryNode %s] : " text "\n", \
		__FUNCTION__, ##__VA_ARGS__)
#else
#	define TRACE(text, ...)
#endif


using namespace Smb;


// #pragma mark - ShareDirectoryNode::Cookie


struct ShareDirectoryNode::Cookie {
	Cookie()
		:
		fDirectoryHandle(NULL)
	{
	}

	SMBCFILE* fDirectoryHandle;
};


// #pragma mark - ShareDirectoryNode


ShareDirectoryNode::ShareDirectoryNode(const BString& url, size_t nameLength,
	Volume* volume, SambaContext* context)
	:
	ShareNode(url, nameLength, volume, context)
{
}


ShareDirectoryNode::ShareDirectoryNode(const ShareDirectoryNode& prototype,
	const BString& newURL, size_t nameLength)
	:
	ShareNode(prototype, newURL, nameLength)
{
}


ShareDirectoryNode::~ShareDirectoryNode()
{
}


NodeType
ShareDirectoryNode::Type() const
{
	return kShareDirectory;
}


void
ShareDirectoryNode::Delete(bool removed, bool reenter)
{
	TRACE("ID=0x%" B_PRIx64 " URL=%s removed=%d reenter=%d\n", fID,
		fURL.String(), removed, reenter);

	// Unless the directory itself was removed (i.e. actually removed in the
	// file share), do nothing here. If the VFS only removed the vnode from its
	// cache (i.e. put_vnode()), but the directory still exists,  we need to
	// keep the node instance around. Applications can still hold a node_ref
	// and create a BDirectory from it at any time and we need to remember
	// the ID->URL mapping then.
	if (removed)
		Node::Delete(removed, reenter);
}


status_t
ShareDirectoryNode::Open(int, void**)
{
	// Samba won't allow us to open() a directory, so to just verify that
	// the path exists, we do a stat() on it
	GlobalSambaLocker sambaLocker(gGlobalSambaLock);
	struct stat st;
	return fSambaContext->Stat(fURL, &st);
}


status_t
ShareDirectoryNode::Close(void*)
{
	// Nothing to do here, see Open()
	return B_OK;
}


// #pragma mark - File-only, just fail


status_t
ShareDirectoryNode::Read(void*, off_t, void*, size_t*)
{
	return B_IS_A_DIRECTORY;
}


status_t
ShareDirectoryNode::Write(void*, off_t, const void*, size_t*)
{
	return B_IS_A_DIRECTORY;
}


// #pragma mark - Directory-only


status_t
ShareDirectoryNode::Lookup(const char* name, ino_t* outNodeID)
{
	BString url(_EntryURL(name));
	TRACE("URL=%s", url.String());

	if (strcmp(name, ".") == 0) {
		*outNodeID = fID;
		return B_OK;
	} else if (strcmp(name, "..") == 0) {
		BString parentURL = fURL;
		int32 lastSlashPosition = parentURL.FindLast('/');
		parentURL.Remove(lastSlashPosition,
			parentURL.Length() - lastSlashPosition);
		AutoLocker<Volume> volumeLocker(fVolume);
		*outNodeID = fVolume->RecallNode(parentURL)->ID();
		return B_OK;
	}

	struct stat st;
	GlobalSambaLocker sambaLocker(gGlobalSambaLock);
	status_t status = fSambaContext->Stat(url, &st);
	sambaLocker.Unlock();

	if (status != B_OK) {
		TRACE("lookup error: %s (0x%lx)", strerror(status), status);
		return status;
	}

	AutoLocker<Volume> volumeLocker(fVolume);

	Node* node = fVolume->RecallNode(url);
	if (node == NULL) {
		if (S_ISDIR(st.st_mode)) {
			TRACE("lookup %s -> is directory", url.String());

			node = new(std::nothrow) ShareDirectoryNode(url, strlen(name),
				fVolume, fSambaContext);
		} else {
			TRACE("lookup %s -> is file", url.String());

			node = new(std::nothrow) ShareFileNode(url, strlen(name),
				fVolume, fSambaContext);
		}
		if (node == NULL)
			return B_NO_MEMORY;

		fVolume->MemorizeNode(node);
	} else {
		TRACE("recalled node ID 0x%" B_PRIx64, node->ID());
	}

	*outNodeID = node->ID();

	return B_OK;
}


status_t
ShareDirectoryNode::Create(const char* name, int openMode, int,
	void** outCookie, ino_t* outNodeID)
{
	BString url(_EntryURL(name));

	GlobalSambaLocker sambaLocker(gGlobalSambaLock);
	SMBCFILE* file = NULL;
	status_t status = fSambaContext->Create(url, openMode, &file);
	sambaLocker.Unlock();
	if (status != B_OK)
		return status;

	AutoLocker<Volume> volumeLocker(fVolume);
	Node* const node = new(std::nothrow) ShareFileNode(url, strlen(name),
		fVolume, fSambaContext);
	fVolume->MemorizeNode(node);
	volumeLocker.Unlock();

	*outCookie = (void*)file;
	*outNodeID = node->ID();

	notify_entry_created(fVolume->ID(), fID, name, node->ID());

	return B_OK;
}


status_t
ShareDirectoryNode::Remove(const char* name)
{
	BString url(_EntryURL(name));

	GlobalSambaLocker sambaLocker(gGlobalSambaLock);
	status_t status = fSambaContext->Unlink(url);
	sambaLocker.Unlock();

	if (status != B_OK)
		return status;

	AutoLocker<Volume> volumeLocker(fVolume);
	Node* const removedNode = fVolume->RecallNode(url);

	remove_vnode(fVolume->VFSVolume(), removedNode->ID());
	volumeLocker.Unlock();

	notify_entry_removed(fVolume->ID(), fID, name, removedNode->ID());

	return B_OK;
}


status_t
ShareDirectoryNode::OpenDir(void** outCookie)
{
	*outCookie = new(std::nothrow) Cookie;

	status_t status = Open(0, NULL);
	if (status != B_OK)
		delete static_cast<Cookie*>(*outCookie);

	return status;
}


status_t
ShareDirectoryNode::CloseDir(void* cookie)
{
	Cookie* const dirCookie = static_cast<Cookie*>(cookie);
	if (dirCookie->fDirectoryHandle == NULL)
		return B_OK;
	GlobalSambaLocker sambaLocker(gGlobalSambaLock);
	return fSambaContext->CloseDir(dirCookie->fDirectoryHandle);
}


status_t
ShareDirectoryNode::Rename(const char* fromName, Node* toDir,
	const char* toName)
{
	BString fromURL(_EntryURL(fromName));
	BString toURL(toDir->URL());
	toURL << "/" << toName;

	GlobalSambaLocker sambaLocker(gGlobalSambaLock);
	status_t status = fSambaContext->Rename(fromURL, toURL);
	sambaLocker.Unlock();

	if (status != B_OK)
		return status;

	AutoLocker<Volume> volumeLocker(fVolume);
	Node* overwritteNode = fVolume->RecallNode(toURL);
	if (overwritteNode != NULL) {
		// toURL already existed, node was overwritten
		remove_vnode(fVolume->VFSVolume(), overwritteNode->ID());
		notify_entry_removed(fVolume->ID(), fID, toName, overwritteNode->ID());
	}

	Node* oldNode = fVolume->RecallNode(fromURL);
	if (oldNode == NULL)
		return B_OK;

	Node* newNode = NULL;
	switch (oldNode->Type()) {
		case kShareDirectory:
			newNode = new(std::nothrow) ShareDirectoryNode(
				*static_cast<ShareDirectoryNode*>(oldNode),
				toURL, strlen(toName));
			break;

		case kShareFile:
			newNode = new(std::nothrow) ShareFileNode(
				*static_cast<ShareFileNode*>(oldNode),
				toURL, strlen(toName));
			break;

		default:
			debugger("unexpected node type");
			break;
	}
	if (newNode == NULL)
		return B_NO_MEMORY;

	fVolume->ForgetNode(oldNode->URL());
	fVolume->MemorizeNode(newNode);

	remove_vnode(fVolume->VFSVolume(), oldNode->ID());

	volumeLocker.Unlock();

	notify_entry_moved(fVolume->ID(), fID, fromName, toDir->ID(), toName,
		newNode->ID());

	return B_OK;
}


status_t
ShareDirectoryNode::ReadDir(void* cookie, struct dirent* buffer,
	size_t bufferSize, uint32* num)
{
	if (*num == 0)
		return B_OK;

	Cookie* const dirCookie = static_cast<Cookie*>(cookie);
	status_t status;

	GlobalSambaLocker sambaLocker(gGlobalSambaLock);
	if (dirCookie->fDirectoryHandle == NULL) {
		status = fSambaContext->OpenDir(fURL, &dirCookie->fDirectoryHandle);
		if (status != B_OK)
			return status;
	}

	uint32 entriesRead = 0;
	size_t bufferBytesLeft = bufferSize;
	struct dirent* currentEntry = buffer;

	while (entriesRead < *num) {
		struct smbc_dirent* smbEntry = NULL;
		status = fSambaContext->GetDirectoryEntry(dirCookie->fDirectoryHandle,
			&smbEntry);

		if (status == B_ENTRY_NOT_FOUND) {
			// End of directory
			status = B_OK;
			break;
		}

		if (status != B_OK) {
			// Error
			break;
		}

		size_t recordLength = sizeof(struct dirent) + smbEntry->namelen;
		recordLength = (recordLength + 7) & -8;
			// Round up to next multiple of 8, as recommended by FS API docs

		if (bufferBytesLeft < recordLength) {
			// Out of room in the output buffer
			if (entriesRead == 0) {
				// Couldn't even read a single entry, return buffer overflow
				// as expected by FS API in this case
				status = B_BUFFER_OVERFLOW;
			}
			break;
		}

		BString url(_EntryURL(smbEntry->name));

		TRACE("got dir entry: %s", url.String());

		Node* node = NULL;
		switch (smbEntry->smbc_type) {
			case SMBC_FILE:
				node = new(std::nothrow) ShareFileNode(url,
					strlen(smbEntry->name), fVolume, fSambaContext);
				break;

			case SMBC_DIR:
				node = new(std::nothrow) ShareDirectoryNode(url,
					strlen(smbEntry->name), fVolume, fSambaContext);
				break;

			default:
				// Skip this entry
				continue;
		}
		if (node == NULL) {
			status = B_NO_MEMORY;
			break;
		}

		currentEntry->d_dev = fVolume->ID();
		currentEntry->d_pdev = 0;
		currentEntry->d_ino = node->ID();
		currentEntry->d_pino = 0;
		currentEntry->d_reclen = recordLength;
		strlcpy(currentEntry->d_name, smbEntry->name,
			bufferBytesLeft - sizeof(struct dirent));

		currentEntry = reinterpret_cast<struct dirent*>(
			(uint8*)currentEntry + recordLength);

		entriesRead++;
		bufferBytesLeft -= recordLength;
	}

	*num = entriesRead;
	return status;
}


status_t
ShareDirectoryNode::FreeDirCookie(void* cookie)
{
	delete static_cast<Cookie*>(cookie);
	return B_OK;
}


status_t
ShareDirectoryNode::RewindDirCookie(void* cookie)
{
	Cookie* const dirCookie = static_cast<Cookie*>(cookie);
	if (dirCookie->fDirectoryHandle == NULL)
		return B_OK;
	GlobalSambaLocker sambaLocker(gGlobalSambaLock);
	return fSambaContext->SeekDir(dirCookie->fDirectoryHandle, 0);
}


status_t
ShareDirectoryNode::CreateDir(const char* name, int permissions)
{
	BString url(_EntryURL(name));

	GlobalSambaLocker sambaLocker(gGlobalSambaLock);
	status_t status = fSambaContext->CreateDir(url, permissions);

	if (status != B_OK)
		return status;

	Node* newNode = new(std::nothrow) ShareDirectoryNode(url,
		strlen(name), fVolume, fSambaContext);

	AutoLocker<Volume> volumeLocker(fVolume);
	fVolume->MemorizeNode(newNode);
	volumeLocker.Unlock();

	notify_entry_created(fVolume->ID(), fID, name, newNode->ID());

	return B_OK;
}


status_t
ShareDirectoryNode::RemoveDir(const char* name)
{
	BString url(_EntryURL(name));

	GlobalSambaLocker sambaLocker(gGlobalSambaLock);
	status_t status = fSambaContext->RemoveDir(url);
	sambaLocker.Unlock();

	if (status != B_OK)
		return status;

	AutoLocker<Volume> volumeLocker(fVolume);
	Node* const removedNode = fVolume->RecallNode(url);

	remove_vnode(fVolume->VFSVolume(), removedNode->ID());
	volumeLocker.Unlock();

	notify_entry_removed(fVolume->ID(), fID, name, removedNode->ID());

	return B_OK;
}

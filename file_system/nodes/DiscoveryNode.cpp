/*
 * Copyright 2017 Julian Harnath <julian.harnath@rwth-aachen.de>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include "DiscoveryNode.h"

#include <private/shared/AutoLocker.h>

#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "SambaContext.h"
#include "ShareDirectoryNode.h"
#include "Volume.h"


//#define TRACE_NODE
#ifdef TRACE_NODE
#	define TRACE(text, ...) \
	fprintf(stderr, "SMB-FS [DiscoveryNode %s] : " text "\n", \
		__FUNCTION__, ##__VA_ARGS__)
#else
#	define TRACE(text, ...)
#endif


using namespace Smb;


// #pragma mark - DiscoveryNode::Cookie


struct DiscoveryNode::Cookie {
	Cookie(Entries::iterator it)
		:
		fOpen(true),
		fIterator(it)
	{
	}

	~Cookie()
	{
		assert(!fOpen);
	}

	bool 				fOpen;
	Entries::iterator	fIterator;
};


// #pragma mark - DiscoveryNode::Entry


DiscoveryNode::Entry::Entry(Node* node)
	:
	fNode(node),
	fWasRemoved(false)
{
}


DiscoveryNode::Entry::~Entry()
{
	if (fWasRemoved)
		fNode->Delete(true, false);
}


// #pragma mark - DiscoveryNode


DiscoveryNode::DiscoveryNode(Volume* volume, SambaContext* context)
	:
	Node(kNetworkNodeID, "smb://", "", volume, context),
	fType(kNetwork),
	fDirOpenCount(0),
	fParent(this)
{
	_FillStat();
	_AddDotDirEntries();
}


DiscoveryNode::DiscoveryNode(const BString& url, size_t nameLength,
	const BString& comment, NodeType type, DiscoveryNode* parent)
	:
	Node(url, nameLength, parent->fVolume, parent->fSambaContext),
	fType(type),
	fDirOpenCount(0),
	fParent(parent),
	fComment(comment)
{
	_FillStat();
	_AddDotDirEntries();
}


DiscoveryNode::DiscoveryNode(const char* name, DiscoveryNode* prototype)
	:
	Node(prototype->ID(), prototype->URL(), name, prototype->fVolume,
		prototype->fSambaContext),
	fType(prototype->fType),
	fDirOpenCount(0),
	fParent(prototype->fParent)
{
	fStat = prototype->fStat;
}


DiscoveryNode::~DiscoveryNode()
{
	delete fStat;
}


NodeType
DiscoveryNode::Type() const
{
	return fType;
}


Node*
DiscoveryNode::AddEntry(NodeType type, const BString& name,
	const BString& comment)
{
	AutoLocker<BLocker> locker(fLock);

	BString url(_EntryURL(name));

	Node* entryNode = NULL;
	if (type == kShare) {
		entryNode = new(std::nothrow) ShareDirectoryNode(url, name.Length(),
			fVolume, fSambaContext);
			// TODO: comment gets ignored here

		// TODO: handle authentication
		struct stat st;
		if (entryNode->ReadStat(&st) != B_OK) {
			delete entryNode;
			entryNode = NULL;
		}
	} else {
		entryNode = new(std::nothrow) DiscoveryNode(url, name.Length(),
			comment, type, this);
	}
	if (entryNode == NULL)
		return NULL;

	AutoLocker<Volume> volumeLocker(fVolume);
	fVolume->MemorizeNode(entryNode);
	volumeLocker.Unlock();

	Entry entry(entryNode);
	fEntries.push_back(entry);

	TRACE("added new entry name=%s URL=%s ID=0x%" B_PRIx64, name.String(),
		url.String(), entryNode->ID());

	return entryNode;
}


ino_t
DiscoveryNode::RemoveEntry(const BString& name)
{
	AutoLocker<BLocker> locker(fLock);

	// TODO: should be more efficient
	for (Entries::iterator it = fEntries.begin(); it != fEntries.end(); it++) {
		Entry& entry = *it;
		if (!entry.fWasRemoved && name == entry.fNode->Name()) {
			if (fDirOpenCount > 0) {
				// Mark entry for deletion later when everyone closed the
				// directory
				entry.fWasRemoved = true;
				return entry.fNode->ID();
			} else {
				// Noone has this directory open, delete right away
				ino_t id = entry.fNode->ID();
				entry.fNode->Delete(true, false);

				fEntries.erase(it);
				return id;
			}
		}
	}

	return kInvalidNodeID;
}


status_t
DiscoveryNode::ReadStat(struct stat* destination)
{
	*destination = *fStat;
	return B_OK;
}


status_t
DiscoveryNode::Open(int mode, void** outCookie)
{
	if (   (mode & O_WRONLY) != 0 || (mode & O_RDWR) != 0
		|| (mode & O_TRUNC) != 0) {
		return B_PERMISSION_DENIED;
	}
	*outCookie = NULL;
	return B_OK;
}


status_t
DiscoveryNode::Close(void*)
{
	return B_OK;
}


status_t
DiscoveryNode::FreeCookie(void*)
{
	return B_OK;
}


status_t
DiscoveryNode::Lookup(const char* name, ino_t* outNodeID)
{
	TRACE("%s : lookup %s", fURL.String(), name);

	if (!_HasEntry(name)) {
		TRACE("entry not found");
		return B_ENTRY_NOT_FOUND;
	}

	if (strcmp(name, ".") == 0) {
		*outNodeID = fID;
		return B_OK;
	} else if (strcmp(name, "..") == 0) {
		*outNodeID = fParent->ID();
		return B_OK;
	}

	BString url(_EntryURL(name));

	AutoLocker<Volume> volumeLocker(fVolume);
	Node* const node = fVolume->RecallNode(url);
	volumeLocker.Unlock();
	if (node == NULL)
		debugger("node in entry list, but not in volume memory");

	*outNodeID = node->ID();

	TRACE("lookup successful, ID=0x%" B_PRIx64, *outNodeID);

	return B_OK;
}


status_t
DiscoveryNode::OpenDir(void** outCookie)
{
	AutoLocker<BLocker> locker(fLock);

	if (fDirOpenCount == 0)
		fVolume->NetworkScan();

	fDirOpenCount++;

	Cookie* cookie = new(std::nothrow) Cookie(fEntries.begin());
	*outCookie = (void*)cookie;
	return B_OK;
}


status_t
DiscoveryNode::CloseDir(void* cookie)
{
	AutoLocker<BLocker> locker(fLock);

	fDirOpenCount--;
	if (fDirOpenCount == 0) {
		// Now we can safely delete all entries which were removed
		Entries::iterator it = fEntries.begin();
		while (it != fEntries.end()) {
			if (it->fWasRemoved)
				it = fEntries.erase(it);
			else
				it++;
		}

		// Noone else has it open anymore, so a good time to do another scan
		fVolume->NetworkScan();
	}

	static_cast<Cookie*>(cookie)->fOpen = false;
	return B_OK;
}


status_t
DiscoveryNode::ReadDir(void* cookie, struct dirent* buffer, size_t bufferSize,
	uint32* num)
{
	AutoLocker<BLocker> locker(fLock);

	Cookie* dirCookie = static_cast<Cookie*>(cookie);
	if (!dirCookie->fOpen)
		return B_ERROR;

	uint32 entriesRead = 0;
	size_t bufferBytesLeft = bufferSize;
	struct dirent* currentEntry = buffer;

	while (entriesRead < *num) {
		status_t status = _GetDirEntry(dirCookie->fIterator, &currentEntry,
			&bufferBytesLeft);

		if (status == B_ENTRY_NOT_FOUND) {
			// End of directory
			break;
		}
		if (status == B_BUFFER_OVERFLOW) {
			// Out of room for next entry
			if (entriesRead == 0) {
				// Couldn't even read a single entry, return buffer overflow
				// as expected by FS API in this case
				return B_BUFFER_OVERFLOW;
			}
			break;
		}
		if (status != B_OK) {
			// Error
			*num = entriesRead;
			return status;
		}

		dirCookie->fIterator++;
		entriesRead++;
	}

	*num = entriesRead;
	return B_OK;
}


status_t
DiscoveryNode::FreeDirCookie(void* cookie)
{
	delete static_cast<Cookie*>(cookie);
	return B_OK;
}


status_t
DiscoveryNode::RewindDirCookie(void* cookie)
{
	static_cast<Cookie*>(cookie)->fIterator = fEntries.begin();
	return B_OK;
}


status_t
DiscoveryNode::Read(void*, off_t, void*, size_t*)
{
	return B_IS_A_DIRECTORY;
}


status_t
DiscoveryNode::Write(void*, off_t, const void*, size_t*)
{
	return B_IS_A_DIRECTORY;
}


status_t
DiscoveryNode::WriteStat(const struct stat*, uint32)
{
	return B_PERMISSION_DENIED;
}


status_t
DiscoveryNode::Create(const char*, int, int, void**, ino_t*)
{
	return B_PERMISSION_DENIED;
}


status_t
DiscoveryNode::Remove(const char*)
{
	return B_PERMISSION_DENIED;
}


status_t
DiscoveryNode::Rename(const char*, Node*, const char*)
{
	return B_PERMISSION_DENIED;
}


status_t
DiscoveryNode::CreateDir(const char*, int)
{
	return B_PERMISSION_DENIED;
}


status_t
DiscoveryNode::RemoveDir(const char*)
{
	return B_PERMISSION_DENIED;
}


status_t
DiscoveryNode::_GetDirEntry(Entries::iterator it, struct dirent** destination,
	size_t* bufferBytesLeft)
{
	if (it == fEntries.end() || it->fWasRemoved)
		return B_ENTRY_NOT_FOUND;

	const Node* entryNode = it->fNode;

	size_t recordLength = sizeof(struct dirent) + strlen(entryNode->Name());
	recordLength = (recordLength + 7) & -8;
		// Round up to next multiple of 8, as recommended by FS API docs
	if (*bufferBytesLeft < recordLength)
		return B_BUFFER_OVERFLOW;

	BString url = _EntryURL(entryNode->Name());
	(*destination)->d_dev = fVolume->ID();
	(*destination)->d_pdev = 0;
	(*destination)->d_ino = entryNode->ID();
	(*destination)->d_pino = 0;
	(*destination)->d_reclen = recordLength;
	strlcpy((*destination)->d_name, entryNode->Name(),
		*bufferBytesLeft - sizeof(struct dirent));

	*destination = reinterpret_cast<struct dirent*>((uint8*)(*destination)
		+ recordLength);
	*bufferBytesLeft -= recordLength;

	return B_OK;
}


bool
DiscoveryNode::_HasEntry(const char* name)
{
	// TODO: should be more efficient
	for (Entries::iterator it = fEntries.begin(); it != fEntries.end(); it++) {
		if (!it->fWasRemoved && strcmp(it->fNode->Name(), name) == 0)
			return true;
	}
	return false;
}


void
DiscoveryNode::_FillStat()
{
	fStat = new(std::nothrow) struct stat;

	fStat->st_dev = fVolume->ID();
	fStat->st_ino = fID;
	fStat->st_mode = S_IFDIR
		| S_IRUSR | S_IXUSR
		| S_IRGRP | S_IXGRP
		| S_IROTH | S_IXOTH;
	fStat->st_nlink = 1;
	fStat->st_uid = getuid();
	fStat->st_gid = getgid();
	fStat->st_size = 4096;
	fStat->st_rdev = 0;
	fStat->st_blksize = 4096;
	_SetStatTimeToNow();
	fStat->st_type = 0;
	fStat->st_blocks = 1;
}


void
DiscoveryNode::_SetStatTimeToNow()
{
	struct timespec now;
	now.tv_sec = time(NULL);
	now.tv_nsec = 0;
	fStat->st_atim = now;
	fStat->st_mtim = now;
	fStat->st_ctim = now;
	fStat->st_crtim = now;
}


void
DiscoveryNode::_AddDotDirEntries()
{
	if (fName != NULL
		&& (strcmp(fName, ".") == 0 || strcmp(fName, "..") == 0)) {
		// "." and ".." dirs themselves don't get further dot dirs as children
		return;
	}

	DiscoveryNode* entryNode = new(std::nothrow) DiscoveryNode(".", this);
	Entry entry(entryNode);
	fEntries.push_back(entry);

	entryNode = new(std::nothrow) DiscoveryNode("..", fParent);
	entry.fNode = entryNode;
	fEntries.push_back(entry);
}


BString
DiscoveryNode::_EntryURL(const char* entryName)
{
	BString url;
	switch (fType) {
		case kNetwork:
			// Root network node has "smb://" as URL, don't add another '/'
			url = fURL;
			url << entryName;
			break;

		case kWorkgroup:
			// Workgroups have servers as children, libsmbclient expects URLs
			// to servers and shares to exclude the workgroup in the path
			url = fParent->URL();
			url << entryName;
			break;

		default:
			url = fURL;
			url << "/" << entryName;
			break;
	}
	return url;
}

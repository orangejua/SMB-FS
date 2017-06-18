/*
 * Copyright 2017 Julian Harnath <julian.harnath@rwth-aachen.de>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include "Volume.h"

#include <Application.h>
#include <fs_interface.h>
#include <kernel/OS.h>
#include <private/shared/AutoLocker.h>
#include <Roster.h>

#include <assert.h>
#include <stdio.h>

#include "nodes/DiscoveryNode.h"
#include "nodes/Node.h"
#include "nodes/ShareDirectoryNode.h"
#include "nodes/ShareFileNode.h"
#include "Protocol.h"
#include "SambaContext.h"


//#define TRACE_VOLUME
#ifdef TRACE_VOLUME
#	define TRACE(text, ...) \
	fprintf(stderr, "SMB-FS [Volume %s] : " text "\n", \
		__FUNCTION__, ##__VA_ARGS__)
#else
#	define TRACE(text, ...)
#endif


using namespace Smb;


Volume::Volume(const char*, uint32, fs_volume* vfsVolume)
	:
	fStatus(B_NO_INIT),
	fSambaContext(new(std::nothrow) SambaContext),
	fVFSVolume(vfsVolume),
	fReadOnly(false),
	fLastScanTime(0),
	fNetworkNode(new(std::nothrow) DiscoveryNode(this, fSambaContext)),
	fNextNodeID(fNetworkNode->ID() + 1),
	fAssistantMessenger(NULL)
{
	_InitFsInfo();
	_RegisterAsMessageHandler();
	fStatus = _LaunchAssistant();

	AutoLocker<BLocker> locker(fLock);
	MemorizeNode(fNetworkNode);
}


Volume::~Volume()
{
	AutoLocker<BLocker> locker(fLock);

	be_app->Lock();
	be_app->RemoveHandler(this);
	be_app->Unlock();

	if (fAssistantMessenger != NULL)
		fAssistantMessenger->SendMessage(kMsgQuit);

	delete fSambaContext;
	delete fAssistantMessenger;

	NodeByURL::Iterator iterator = fNodeURLMemory.GetIterator();
	while (iterator.HasNext()) {
		Node* node = *(iterator.NextValue());
		if (node->Type() == kShareDirectory)
			delete node;
	}
	fNodeIDMemory.Clear();
	fNodeURLMemory.Clear();
}


// #pragma mark - Generic


status_t
Volume::InitCheck() const
{
	return fStatus;
}


dev_t
Volume::ID() const
{
	return fVFSVolume->id;
}


fs_volume*
Volume::VFSVolume() const
{
	return fVFSVolume;
}


// #pragma mark - File system


void
Volume::NetworkScan()
{
	TRACE("request network scan");

	if (system_time() < fLastScanTime + kMinScanInterval) {
		TRACE("scan request ignored");
		return;
	}

	fAssistantMessenger->SendMessage(kMsgScan);
}


status_t
Volume::Unmount()
{
	return B_OK;
}


status_t
Volume::FsInfo(struct fs_info* info)
{
	*info = fFsInfo;
	return B_OK;
}


// #pragma mark - Nodes


/*! Volume must be locked
*/
ino_t
Volume::MakeFreshNodeID()
{
	assert(fLock.IsLocked());
	ino_t id = fNextNodeID++;
	if (id < 0)
		debugger("woah, you overflowed an int64");
	return id;
}


/*! Volume must be locked
*/
void
Volume::MemorizeNode(Node* node)
{
	TRACE("memorize node, URL=%s ID=0x%" B_PRIx64, node->URL().String(),
		node->ID());

	assert(fLock.IsLocked());
	assert(RecallNode(node->URL()) == NULL);
		// node must not be known yet
	assert(node->ID() != kInvalidNodeID);
		// node must already have an ID

	fNodeIDMemory.Put(node->ID(), node);
	fNodeURLMemory.Put(node->URL().String(), node);
}


/*! Volume must be locked
*/
Node*
Volume::RecallNode(const BString& url)
{
	assert(fLock.IsLocked());
	return fNodeURLMemory.Get(url.String());
}


/*! Volume must be locked
*/
Node*
Volume::ForgetNode(const BString& url)
{
	TRACE("forget node URL=%s", url.String());

	assert(fLock.IsLocked());

	Node* const node = fNodeURLMemory.Remove(url.String());
	if (node == NULL)
		return NULL;
	fNodeIDMemory.Remove(node->ID());
	return node;
}


status_t
Volume::Lookup(Node* directory, const char* name, ino_t* outNodeID)
{
	BString url(directory->URL());
	url << "/" << name;

	AutoLocker<BLocker> locker(fLock);
	Node* const node = fNodeURLMemory.Get(url.String());
	if (node != NULL) {
		*outNodeID = node->ID();
		return B_OK;
	}
	locker.Unlock();

	return directory->Lookup(name, outNodeID);
}


status_t
Volume::GetVNode(ino_t id, void** outNode)
{
	AutoLocker<BLocker> locker(fLock);
	Node* const node = fNodeIDMemory.Get(id);
	if (node == NULL)
		return B_ENTRY_NOT_FOUND;

	*outNode = (void*)node;
	return B_OK;
}


Node*
Volume::RootNode()
{
	return fNetworkNode;
}


// #pragma mark - BHandler interface


void
Volume::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgScanFinished:
			TRACE("scan finished");
			fLastScanTime = system_time();
			break;

		case kMsgFoundResource:
		{
			TRACE("found resource");

			int8 type;
			BString dirURL, name, comment;
			status_t status = message->FindInt8("type", &type);
			if (status != B_OK
				|| !node_type_valid(static_cast<NodeType>(type)))
				return;
			status = message->FindString("directory url", &dirURL);
			if (status != B_OK)
				return;
			status = message->FindString("name", &name);
			if (status != B_OK)
				return;
			status = message->FindString("comment", &comment);
			if (status != B_OK)
				return;
			_FoundResource(static_cast<NodeType>(type), dirURL, name, comment);
			break;
		}

		case kMsgLostResource:
		{
			TRACE("lost resource");

			BString dirURL, name;
			status_t status = message->FindString("directory url", &dirURL);
			if (status != B_OK)
				return;
			status = message->FindString("name", &name);
			if (status != B_OK)
				return;
			_LostResource(dirURL, name);
			break;
		}

		default:
			BHandler::MessageReceived(message);
			break;
	}
}


// #pragma mark - Internal


void
Volume::_InitFsInfo()
{
	memset(&fFsInfo, 0, sizeof(fFsInfo));

	fFsInfo.block_size = 4096;
	fFsInfo.io_size = 128 * 1024;
		// Empirically determined to yield good performance

	fFsInfo.total_blocks = (100ULL * 1024 * 1024 * 1024) / fFsInfo.block_size;
	fFsInfo.free_blocks = fFsInfo.total_blocks;
	fFsInfo.total_nodes = LONGLONG_MAX;
	fFsInfo.free_nodes = fFsInfo.total_nodes;
		// Since this is a "virtual" volume containing all the SMB network's
		// workgroups/servers/shares, there's no useful way to give the volume
		// a size or node count. So we just assign sufficiently large values
		// so the user doesn't get "out of space" errors.

	strlcpy(fFsInfo.volume_name, "SMB Network", sizeof(fFsInfo.volume_name));

	fFsInfo.flags = B_FS_IS_PERSISTENT | B_FS_IS_SHARED;
		// | B_FS_HAS_ATTR | B_FS_HAS_MIME;
	if (fReadOnly)
		fFsInfo.flags |= B_FS_IS_READONLY;
}


void
Volume::_RegisterAsMessageHandler()
{
	be_app->Lock();
	be_app->AddHandler(this);
	be_app->SetPreferredHandler(this);
	be_app->Unlock();
}


status_t
Volume::_LaunchAssistant()
{
	status_t status = be_roster->Launch(kAssistantSignature);
	if (status == B_OK) {
		fAssistantMessenger = new BMessenger(kAssistantSignature);
		if (!fAssistantMessenger->IsValid())
			return B_ERROR;
		NetworkScan();
	}
	return status;
}


void
Volume::_FoundResource(NodeType type, const BString& dirURL,
	const BString& name, const BString& comment)
{
	TRACE("add resource dir=%s name=%s comment=%s",
		dirURL.String(), name.String(), comment.String());

	AutoLocker<BLocker> locker(fLock);
	Node* const dirNode = fNodeURLMemory.Get(dirURL.String());
	if (dirNode == NULL)
		debugger("directory not found");

	DiscoveryNode* const discoveryDirNode
		= dynamic_cast<DiscoveryNode*>(dirNode);
	if (discoveryDirNode == NULL)
		debugger("unexpected node type");

	Node* const newNode = discoveryDirNode->AddEntry(type, name, comment);

	if (newNode != NULL) {
		notify_entry_created(ID(), dirNode->ID(), name.String(),
			newNode->ID());
	}
}


void
Volume::_LostResource(const BString& dirURL, const BString& name)
{
	TRACE("remove resource dir=%s name=%s",
		dirURL.String(), name.String());

	AutoLocker<BLocker> locker(fLock);
	Node* const dirNode = fNodeURLMemory.Get(dirURL.String());
	if (dirNode == NULL)
		debugger("directory not found");

	DiscoveryNode* const discoveryDirNode
		= dynamic_cast<DiscoveryNode*>(dirNode);
	if (discoveryDirNode == NULL)
		debugger("unexpected node type");

	ino_t id = discoveryDirNode->RemoveEntry(name);
	if (id == kInvalidNodeID)
		debugger("entry not found");

	notify_entry_removed(ID(), dirNode->ID(), name.String(), id);
}

/*
 * Copyright 2017 Julian Harnath <julian.harnath@rwth-aachen.de>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include "Assistant.h"

#include <Messenger.h>
#include <MimeType.h>
#include <Path.h>
#include <Resources.h>
#include <String.h>
#include <kernel/image.h>

#include <queue>

#include <stdio.h>

#include "Protocol.h"
#include "SambaContext.h"
#include "TreeNode.h"


//#define TRACE_ASSISTANT
#ifdef TRACE_ASSISTANT
#	define TRACE(text, ...) \
	fprintf(stderr, "SMB-FS-Assistant [%s] : " text "\n", \
		__FUNCTION__, ##__VA_ARGS__)
#else
#	define TRACE(text, ...)
#endif


using namespace Smb;


struct Assistant::DirHandleCloser {
	DirHandleCloser(SMBCFILE* handle, SambaContext* context)
		:
		fHandle(handle),
		fSambaContext(context)
	{
	}

	~DirHandleCloser()
	{
		fSambaContext->CloseDir(fHandle);
	}

	SMBCFILE* fHandle;
	SambaContext* fSambaContext;
};


Assistant::Assistant()
	:
	BApplication(kAssistantSignature),
	fSambaContext(new SambaContext),
	fNetworkTree(new TreeNode),
	fLastScanTime(0),
	fSmbFsMessenger(NULL)
{
}


Assistant::~Assistant()
{
}


void
Assistant::MessageReceived(BMessage* message)
{
	TRACE("got message");

	if (fSmbFsMessenger == NULL && message->IsSourceRemote()) {
		// TODO: check that it really comes from SMB-FS
		fSmbFsMessenger = new BMessenger(message->ReturnAddress());
	}

	switch (message->what) {
		case kMsgConfigure:
			// TODO
			break;

		case kMsgStatus:
			// TODO
			break;

		case kMsgScan:
			_Scan();
			break;

		case kMsgQuit:
			be_app->PostMessage(B_QUIT_REQUESTED);
			break;

		default:
			BApplication::MessageReceived(message);
			break;
	}
}


void
Assistant::_Scan()
{
	if (system_time() < fLastScanTime + 10 * 1000 * 1000) {
		TRACE("scan request ignored");
		return;
	}

	TRACE("scan request");

	GlobalSambaLocker sambaLocker(gGlobalSambaLock);

	TreeNode* newTree = new TreeNode;

	std::queue<TreeNode*> scanQueue;
	scanQueue.push(newTree);

	while (!scanQueue.empty()) {
		TreeNode* node = scanQueue.front();
		scanQueue.pop();

		TRACE("inspect %s", node->URL().String());

		SMBCFILE* dirHandle = NULL;
		status_t status = fSambaContext->OpenDir(node->URL(), &dirHandle);
		if (status != B_OK) {
			TRACE("failed to open %s : %s", node->URL().String(),
				strerror(status));
			continue;
		}
		DirHandleCloser handleCloser(dirHandle, fSambaContext);

		for (;;) {
			struct smbc_dirent* entry = NULL;
			status = fSambaContext->GetDirectoryEntry(dirHandle, &entry);
			if (status == B_ENTRY_NOT_FOUND) {
				TRACE("no more entries");
				break;
			}
			if (status != B_OK) {
				TRACE("skip entry: %s", strerror(errno));
				break;
			}

			TRACE("look at entry %s", entry->name);

			TreeNode* newNode = NULL;
			switch (entry->smbc_type) {
				case SMBC_WORKGROUP:
					TRACE("is workgroup entry");
					newNode = node->AddWorkgroup(entry->name);
					scanQueue.push(newNode);
					break;

				case SMBC_SERVER:
					TRACE("is server entry");
					newNode = node->AddServer(entry->name, entry->comment);
					scanQueue.push(newNode);
					break;

				case SMBC_FILE_SHARE:
					TRACE("is file share entry");
					newNode = node->AddShare(entry->name, entry->comment);
					break;

				default:
					TRACE("is other entry, skip");
					continue;
			}
		}
	}

	TRACE("scan finished");

	newTree->Sort();

	_TreeDiff(fNetworkTree, newTree);

	delete fNetworkTree;
	fNetworkTree = newTree;

	BMessage message(kMsgScanFinished);
	status_t status = fSmbFsMessenger->SendMessage(&message);
	if (status != B_OK) {
		TRACE("failed to send message: %s", strerror(status));
	}

	fLastScanTime = system_time();
}


void
Assistant::_TreeDiff(TreeNode* oldTree, TreeNode* newTree)
{
	if (oldTree->ChildCount() == 0) {
		if (newTree->ChildCount() == 0) {
			// Both empty
			return;
		}

		// All children are new
		for (uint32 i = 0; i < newTree->ChildCount(); i++)
			_NotifyNodeAdded(&newTree->ChildAt(i));
		return;
	}

	if (newTree->ChildCount() == 0) {
		// All children are gone
		for (uint32 i = 0; i < oldTree->ChildCount(); i++)
			_NotifyNodeRemoved(&oldTree->ChildAt(i));
		return;
	}

	uint32 o = 0, n = 0;
	while (o < oldTree->ChildCount() && n < newTree->ChildCount()) {
		if (oldTree->ChildAt(o) < newTree->ChildAt(n)) {
			// Child is in old tree, but not in new
			_NotifyNodeRemoved(&oldTree->ChildAt(o));
			o++;
		} else if (oldTree->ChildAt(o) > newTree->ChildAt(n)) {
			// Child is in new tree, but not in old
			_NotifyNodeAdded(&newTree->ChildAt(n));
			n++;
		} else {
			// Child is in both trees, compare the grandchildren
			_TreeDiff(&oldTree->ChildAt(o), &newTree->ChildAt(n));
			o++;
			n++;
		}
	}

	// Remaining children from old tree
	while (o < oldTree->ChildCount())
		_NotifyNodeRemoved(&oldTree->ChildAt(o++));

	// Remaining children from new tree
	while (n < newTree->ChildCount())
		_NotifyNodeAdded(&newTree->ChildAt(n++));
}


void
Assistant::_NotifyNodeAdded(TreeNode* node)
{
	assert(fSmbFsMessenger != NULL);
	TRACE("notify new node: %s", node->URL().String());

	BMessage message(kMsgFoundResource);
	message.AddInt8("type", node->Type());
	message.AddString("directory url", node->Parent()->URL());
	message.AddString("name", node->Name());
	message.AddString("comment", node->Comment());

	fSmbFsMessenger->SendMessage(&message);

	for (uint32 i = 0; i < node->ChildCount(); i++)
		_NotifyNodeAdded(&node->ChildAt(i));
}


void
Assistant::_NotifyNodeRemoved(TreeNode* node)
{
	assert(fSmbFsMessenger != NULL);
	TRACE("notify removed node: %s", node->URL().String());

	BMessage message(kMsgLostResource);
	message.AddString("directory url", node->Parent()->URL());
	message.AddString("name", node->Name());

	fSmbFsMessenger->SendMessage(&message);
}

/*
 * Copyright 2017 Julian Harnath <julian.harnath@rwth-aachen.de>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#ifndef SMBFS_VOLUME_H
#define SMBFS_VOLUME_H

#include <Handler.h>
#include <String.h>

#include <fs_interface.h>
#include <kernel/fs_info.h>
#include <private/shared/HashMap.h>
#include <private/shared/HashString.h>

#include "NodeDefs.h"


namespace Smb {


class Node;
class SambaContext;


class Volume : public BHandler {
public:
							Volume(const char* args, uint32 flags,
								fs_volume* vfsVolume);
							~Volume();

// ----- Generic --------------------------------------------------------------
			status_t		InitCheck() const;
			dev_t			ID() const;
			fs_volume*		VFSVolume() const;

// ----- File system ----------------------------------------------------------
			void			NetworkScan();
			status_t		Unmount();
			status_t		FsInfo(struct fs_info* info);

// ----- Nodes ----------------------------------------------------------------
			ino_t			MakeFreshNodeID();					// must lock
			void			MemorizeNode(Node* node);			// must lock
			Node*			RecallNode(const BString& url);		// must lock
			Node*			ForgetNode(const BString& url);		// must lock

			status_t		Lookup(Node* directory, const char* name,
								ino_t* outNodeID);
			status_t		GetVNode(ino_t id, void** outNode);
			Node*			RootNode();

// ----- BHandler interface ---------------------------------------------------
	virtual	void			MessageReceived(BMessage* message);

// ----- Lockable interface ---------------------------------------------------
			bool			Lock()   { return fLock.Lock(); }
			void			Unlock() { fLock.Unlock(); }

private:
			void			_InitFsInfo();
			void			_RegisterAsMessageHandler();
			status_t		_LaunchAssistant();

			void			_FoundResource(NodeType type, const BString& dirURL,
								const BString& name, const BString& comment);
			void			_LostResource(const BString& dirURL,
								const BString& name);

private:
	typedef HashMap<HashKey64<ino_t>, Node*> NodeByID;
	typedef HashMap<HashString, Node*> NodeByURL;

	enum {
		kMinScanInterval = 5 * 1000 * 1000 // µsec
	};

private:
			status_t		fStatus;
			BLocker			fLock;

			SambaContext*	fSambaContext;
			fs_volume*		fVFSVolume;
			bool			fReadOnly;
			struct fs_info	fFsInfo;
			bigtime_t		fLastScanTime;

			Node*			fNetworkNode;	// root node
			ino_t			fNextNodeID;
			NodeByID		fNodeIDMemory;
			NodeByURL		fNodeURLMemory;

			BMessenger*		fAssistantMessenger;
};


} // namespace Smb


#endif // SMBFS_VOLUME_H

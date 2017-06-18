/*
 * Copyright 2017 Julian Harnath <julian.harnath@rwth-aachen.de>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#ifndef SMBFS_ASSITANT_H
#define SMBFS_ASSITANT_H

#include <Application.h>

#include <ObjectList.h>


class BMessenger;


namespace Smb {


class SambaContext;
class TreeNode;


class Assistant : public BApplication {
public:
								Assistant();
	virtual						~Assistant();

	virtual	void				MessageReceived(BMessage* message);

private:
			void				_Scan();
			void				_TreeDiff(TreeNode* oldTree,
									TreeNode* newTree);

			void				_NotifyNodeAdded(TreeNode* node);
			void				_NotifyNodeRemoved(TreeNode* node);

private:
	struct DirHandleCloser;

private:
			SambaContext*		fSambaContext;
			TreeNode*			fNetworkTree;

			bigtime_t			fLastScanTime;

			BMessenger*			fSmbFsMessenger;
};


} // namespace Smb


#endif // SMBFS_ASSITANT_H

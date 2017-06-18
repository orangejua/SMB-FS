/*
 * Copyright 2017 Julian Harnath <julian.harnath@rwth-aachen.de>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#ifndef SMBFS_TREE_NODE_H
#define SMBFS_TREE_NODE_H

#include <String.h>
#include <SupportDefs.h>

#include <vector>

#include "NodeDefs.h"


namespace Smb {


class TreeNode {
private:
								TreeNode(TreeNode* parent,
									NodeType type,
									const BString& name,
									const BString& comment,
									const BString& url);

public:
								TreeNode();
								~TreeNode();

			const BString&		Name() const;
			const BString&		Comment() const;
			const BString&		URL() const;
			NodeType			Type() const;

			TreeNode*			AddWorkgroup(const BString& name);
			TreeNode*			AddServer(const BString& name,
									const BString& comment);
			TreeNode*			AddShare(const BString& name,
									const BString& comment);

			uint32				ChildCount() const;
			TreeNode&			ChildAt(uint32 index);

			TreeNode*			Parent() const;

			void				Sort();

			bool				operator<(const TreeNode& other) const;
			bool				operator>(const TreeNode& other) const;

private:
			TreeNode*			_AddChild(NodeType type, const BString& name,
									const BString& comment);

private:
	typedef std::vector<TreeNode*> NodeArray;

private:
			TreeNode*			fParent;

			NodeType			fType;
			BString				fName;
			BString				fComment;
			BString				fURL;

			NodeArray			fChildren;
};


} // namespace Smb


#endif // SMBFS_TREE_NODE_H

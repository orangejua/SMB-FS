/*
 * Copyright 2017 Julian Harnath <julian.harnath@rwth-aachen.de>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include "TreeNode.h"

#include <assert.h>

#include <algorithm>


using namespace Smb;


namespace {

struct SortFunctor {
	bool operator()(TreeNode* x, TreeNode* y) const
	{
		return *x < *y;
	}
};

}


TreeNode::TreeNode(TreeNode* parent, NodeType type, const BString& name,
	const BString& comment, const BString& url)
	:
	fParent(parent),
	fType(type),
	fName(name),
	fComment(comment),
	fURL(url)
{
}


TreeNode::TreeNode()
	:
	fParent(NULL),
	fType(kNetwork),
	fURL("smb://")
{
}


TreeNode::~TreeNode()
{
	for (uint32 i = 0; i < fChildren.size(); i++)
		delete fChildren[i];
}


const BString&
TreeNode::Name() const
{
	return fName;
}


const BString&
TreeNode::Comment() const
{
	return fComment;
}


const BString&
TreeNode::URL() const
{
	return fURL;
}


NodeType
TreeNode::Type() const
{
	return fType;
}


TreeNode*
TreeNode::AddWorkgroup(const BString& name)
{
	assert(fType == kNetwork);
	return _AddChild(kWorkgroup, name, "");
}


TreeNode*
TreeNode::AddServer(const BString& name, const BString& comment)
{
	assert(fType == kWorkgroup);
	return _AddChild(kServer, name, comment);
}


TreeNode*
TreeNode::AddShare(const BString& name, const BString& comment)
{
	assert(fType == kServer);
	return _AddChild(kShare, name, comment);
}


uint32
TreeNode::ChildCount() const
{
	return fChildren.size();
}


TreeNode&
TreeNode::ChildAt(uint32 index)
{
	assert(index < ChildCount());
	return *(fChildren[index]);
}


TreeNode*
TreeNode::Parent() const
{
	return fParent;
}


void
TreeNode::Sort()
{
	if (ChildCount() == 0)
		return;

	SortFunctor sorter;
	std::sort(fChildren.begin(), fChildren.end(), sorter);

	for (uint32 i = 0; i < ChildCount(); i++)
		fChildren[i]->Sort();
}


bool
TreeNode::operator<(const TreeNode& other) const
{
	assert(Type() == other.Type());
	if (Name() != other.Name())
		return Name() < other.Name();
	else
		return Comment() < other.Comment();
}


bool
TreeNode::operator>(const TreeNode& other) const
{
	return other < *this;
}


TreeNode*
TreeNode::_AddChild(NodeType type, const BString& name, const BString& comment)
{
	BString url;
	switch (fType) {
		case kNetwork:
			url = fURL;
			url << name;
			break;

		case kWorkgroup:
			url = fParent->URL();
			url << name;
			break;

		default:
			url = fURL;
			url << "/" << name;
			break;
	}
	TreeNode* node = new TreeNode(this, type, name, comment, url);
	fChildren.push_back(node);
	return node;
}

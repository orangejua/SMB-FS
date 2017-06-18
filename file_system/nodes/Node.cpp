/*
 * Copyright 2017 Julian Harnath <julian.harnath@rwth-aachen.de>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include "Node.h"

#include <private/shared/AutoLocker.h>

#include <assert.h>
#include <stdio.h>
#include <sys/stat.h>

#include "Volume.h"


//#define TRACE_NODE
#ifdef TRACE_NODE
#	define TRACE(text, ...) \
	fprintf(stderr, "SMB-FS [Node %s] : " text "\n", \
		__FUNCTION__, ##__VA_ARGS__)
#else
#	define TRACE(text, ...)
#endif


using namespace Smb;


Node::Node(const BString& url, size_t nameLength, Volume* volume,
	SambaContext* context)
	:
	fVolume(volume),
	fSambaContext(context),
	fURL(url),
	fName(fURL.String() + fURL.Length() - nameLength),
	fID(fVolume->Lock() ? fVolume->MakeFreshNodeID() : (ino_t)kInvalidNodeID)
{
	assert(nameLength > 0);
	assert((size_t)fURL.Length() > nameLength);

	fVolume->Unlock();
}


Node::Node(ino_t id, const BString& url, const char* name, Volume* volume,
	SambaContext* context)
	:
	fVolume(volume),
	fSambaContext(context),
	fURL(url),
	fName(name),
	fID(id)
{
}


Node::Node(const Node& prototype, const BString& newURL, size_t nameLength)
	:
	fVolume(prototype.fVolume),
	fSambaContext(prototype.fSambaContext),
	fURL(newURL),
	fName(fURL.String() + fURL.Length() - nameLength),
	fID(prototype.fID)
{
}


Node::~Node()
{
}


const char*
Node::Name() const
{
	return fName;
}


const BString&
Node::URL() const
{
	return fURL;
}


ino_t
Node::ID() const
{
	return fID;
}


mode_t
Node::StatType() const
{
	if (Type() == kShareFile)
		return S_IFREG;
	else
		return S_IFDIR;
}


void
Node::Delete(bool, bool reenter)
{
	TRACE("ID=0x%" B_PRIx64 " URL=%s reenter=%d", fID, fURL.String(),
		reenter);

	AutoLocker<Volume> locker(fVolume, reenter);
	fVolume->ForgetNode(fURL);
	locker.Unlock();

	delete this;
}


BString
Node::_EntryURL(const char* entryName)
{
	BString url(fURL);
	url << "/" << entryName;
	return url;
}

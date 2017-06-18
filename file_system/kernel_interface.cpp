/*
 * Copyright 2017 Julian Harnath <julian.harnath@rwth-aachen.de>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <fs_interface.h>

#include <private/shared/AutoDeleter.h>

#include <stdio.h>

#include "nodes/Node.h"
#include "Volume.h"


//#define TRACE_FS_INTERFACE
#ifdef TRACE_FS_INTERFACE
#	define TRACE(text, ...) \
	fprintf(stderr, "SMB-FS [%s] : " text "\n", __FUNCTION__, ##__VA_ARGS__)
#else
#	define TRACE(text, ...)
#endif


extern fs_volume_ops gSmbVolumeOps;
extern fs_vnode_ops gSmbVnodeOps;


// #pragma mark - Utility


static Smb::Volume*
to_smb(fs_volume* volume)
{
	return static_cast<Smb::Volume*>(volume->private_volume);
}


static Smb::Node*
to_smb(fs_vnode* vnode)
{
	return static_cast<Smb::Node*>(vnode->private_node);
}


// #pragma mark - File system


/*!	file_system_module_info::mount

	Mount volume with parameters
	Create volume handle and set volume ops
	Call publish_vnode() for root node, return its ID in rootVnodeId
*/
static status_t
smb_mount(fs_volume* volume, const char* device, uint32 flags,
	const char* args, ino_t* rootVnodeId)
{
	TRACE("volume=%p device=%s flags=0x%" B_PRIx32 " args=%s rootVnodeId=%p",
		volume, device, flags, args, rootVnodeId);
	(void)device;

	Smb::Volume* smbVolume = new(std::nothrow) Smb::Volume(args, flags,
		volume);
	if (smbVolume == NULL)
		return B_NO_MEMORY;

	ObjectDeleter<Smb::Volume> volumeDeleter(smbVolume);

	status_t status = smbVolume->InitCheck();
	if (status != B_OK)
		return status;

	status = publish_vnode(volume, smbVolume->RootNode()->ID(),
		smbVolume->RootNode(), &gSmbVnodeOps,
		smbVolume->RootNode()->StatType(), 0);
	if (status != B_OK) {
		TRACE("failed to publish root node");
		return status;
	}

	volume->private_volume = smbVolume;
	volume->ops = &gSmbVolumeOps;

	*rootVnodeId = smbVolume->RootNode()->ID();

	volumeDeleter.Detach();

	return B_OK;
}


/*!	fs_volume_ops::unmount

	Unmount volume
	Free all resources
*/
static status_t
smb_unmount(fs_volume* volume)
{
	TRACE("volume=%p", volume);

	status_t status = to_smb(volume)->Unmount();
	delete to_smb(volume);
	return status;
}


/*!	fs_volume_ops::read_fs_info

	Fill in info fields: flags, block_size, io_size, total_blocks, free_blocks,
						 total_nodes, free_nodes, volume_name
*/
static status_t
smb_read_fs_info(fs_volume* volume, struct fs_info* info)
{
	TRACE("");
	return to_smb(volume)->FsInfo(info);
}


// #pragma mark - Nodes


/*!	fs_vnode_ops::lookup

	Resolve path name to vnode
	Lookup directory entry -> node
	Should be efficient
	If directory/name exists, use get_vnode() and return ID in id
	Must be able to resolve "." and ".."
*/
static status_t
smb_lookup(fs_volume* volume, fs_vnode* directory, const char* name,
	ino_t* id)
{
	TRACE("dir=%s name=%s", to_smb(directory)->URL().String(), name);

	status_t status = to_smb(volume)->Lookup(to_smb(directory), name, id);
	if (status != B_OK)
		return status;

	void* privateNode = NULL;
	return get_vnode(volume, *id, &privateNode);
}


/*!	fs_volume_ops::get_vnode_name
*/
static status_t
smb_get_vnode_name(fs_volume*, fs_vnode* vnode, char* buffer,
	size_t bufferSize)
{
	const BString& name = to_smb(vnode)->Name();
	strlcpy(buffer, name.String(), bufferSize);
	return B_OK;
}


/*!	fs_volume_ops::get_vnode

	Create private data handle for node with given ID
	VFS calls this when it creates vnode for node
	Initialize vnode->private_node with handle for node
	Set vnode->ops to operation vector for node
	Set type to node type (stat::st_mode)
	Set flags to ORed vnode flags
*/
static status_t
smb_get_vnode(fs_volume* volume, ino_t id, fs_vnode* vnode, int* type,
	uint32* flags, bool)
{
	TRACE("ID=0x%" B_PRIx64, id);
	status_t status = to_smb(volume)->GetVNode(id, &vnode->private_node);
	if (status != B_OK)
		return status;

	vnode->ops = &gSmbVnodeOps;

	*type = to_smb(vnode)->StatType();
	*flags = 0;

	return B_OK;
}


/*!	fs_vnode_ops::put_vnode

	Delete private data handle from node
	File itself is not deleted
*/
static status_t
smb_put_vnode(fs_volume*, fs_vnode* vnode, bool reenter)
{
	TRACE("");
	to_smb(vnode)->Delete(false, reenter);
	return B_OK;
}


/*!	fs_vnode_ops::remove_vnode
*/
static status_t
smb_remove_vnode(fs_volume*, fs_vnode* vnode, bool reenter)
{
	TRACE("");
	to_smb(vnode)->Delete(true, reenter);
	return B_OK;
}


/*!	fs_vnode_ops::read_stat

	Get stat data for node
	Must fill in all stat values except st_dev, st_ino, st_rdev and st_type
*/
static status_t
smb_read_stat(fs_volume*, fs_vnode* vnode, struct stat* fileStat)
{
	TRACE("URL=%s", to_smb(vnode)->URL().String());
	return to_smb(vnode)->ReadStat(fileStat);
}


/*!	fs_vnode_ops::write_stat

	Update file stat
*/
static status_t
smb_write_stat(fs_volume*, fs_vnode* vnode, const struct stat* stat,
	uint32 statMask)
{
	return to_smb(vnode)->WriteStat(stat, statMask);
}


/*!	fs_vnode_ops::access

	Check whether user is allowed to access node with mode (one or more of
		R_OK W_OK X_OK)
	If not allowed, return B_NOT_ALLOWED
	If write access on read-only volume requested, return B_READ_ONLY_DEVICE
*/
static status_t
smb_access(fs_volume*, fs_vnode*, int)
{
	// TODO
	return B_OK;
}


/*! fs_vnode_ops::open

	Open node, called when file is opened
	Create a cookie value which later operations will get
	Store open mode in file cookie
	Mode can be O_RDONLY, O_WRONLY, O_RDWR
	Relevant additional flags O_TRUNC, O_NONBLOCK
*/
static status_t
smb_open(fs_volume*, fs_vnode* vnode, int openMode, void** cookie)
{
	TRACE("URL=%s mode=0x%x", to_smb(vnode)->URL().String(), openMode);
	return to_smb(vnode)->Open(openMode, cookie);
}


/*!	fs_vnode_ops::close

	Close node
	If other threads have blocking read/write/etc operations going on we must
		unblock them now (if we support blocking I/O).
	Mark the cookie so that no further operations can be done with it.
*/
static status_t
smb_close(fs_volume*, fs_vnode* vnode, void* cookie)
{
	TRACE("");
	return to_smb(vnode)->Close(cookie);
}


/*!	fs_vnode_ops::free_cookie

	Free node cookie
	Gets called after close when no other thread uses it anymore
*/
static status_t
smb_free_cookie(fs_volume*, fs_vnode* vnode, void* cookie)
{
	return to_smb(vnode)->FreeCookie(cookie);
}


/*!	fs_vnode_ops::rename

	Rename/move entry
*/
static status_t
smb_rename(fs_volume*, fs_vnode* fromDir, const char* fromName,
	fs_vnode* toDir, const char* toName)
{
	return to_smb(fromDir)->Rename(fromName, to_smb(toDir), toName);
}


/*! fs_vnode_ops::unlink

	Remove non-directory node
	Fails on directories
*/
static status_t
smb_unlink(fs_volume* volume, fs_vnode* dir, const char* name)
{
	ino_t removedNodeId = 0;
	status_t status = to_smb(volume)->Lookup(to_smb(dir), name,
		&removedNodeId);
	if (status != B_OK)
		return status;
	status = to_smb(dir)->Remove(name);
	if (status != B_OK)
		return status;
	return remove_vnode(volume, removedNodeId);
}


// #pragma mark - Files


/*!	fs_vnode_ops::read

	Read data from file
	Fails if node is not a file, cookie not open for reading, pos negative
	'length' contains buffer size
	Store number bytes read in 'length'
*/
static status_t
smb_read(fs_volume*, fs_vnode* vnode, void* cookie, off_t pos,
	void* buffer, size_t* length)
{
	return to_smb(vnode)->Read(cookie, pos, buffer, length);
}


/*!	fs_vnode_ops::write

	Write data to file
	Fails if node is not a file, cookie not open for writing, pos negative
	'length' contains write size
	Store number bytes written in 'length'
*/
static status_t
smb_write(fs_volume*, fs_vnode* vnode, void* cookie, off_t pos,
	const void* buffer, size_t* length)
{
	TRACE("URL=%s", to_smb(vnode)->URL().String());
	return to_smb(vnode)->Write(cookie, pos, buffer, length);
}


/*!	fs_vnode_ops::create

	Like fs_vnode_ops::open, but file is created if it doesn't exist yet
	Fails with B_FILE_EXISTS if file already exists and 'openMode' contains
		flag O_EXCL
*/
static status_t
smb_create(fs_volume* volume, fs_vnode* dir, const char* name, int openMode,
	int permissions, void** cookie, ino_t* newVnodeId)
{
	TRACE("dirURL=%s name=%s mode=0x%x", to_smb(dir)->URL().String(), name,
		openMode);

	status_t status = to_smb(dir)->Create(name, openMode, permissions, cookie,
		newVnodeId);
	if (status != B_OK)
		return status;

	void* privateNode = NULL;
	return get_vnode(volume, *newVnodeId, &privateNode);
}


// #pragma mark - Directories


/*!	fs_vnode_ops::open_dir

	Open directory node
	Fails if node is not a directory
	Store directory cookie in cookie
	Next call to read_dir should return first dir entry
*/
static status_t
smb_open_dir(fs_volume*, fs_vnode* dir, void** cookie)
{
	TRACE("URL=%s", to_smb(dir)->URL().String());
	return to_smb(dir)->OpenDir(cookie);
}


/*!	fs_vnode_ops::close_dir

	Close directory
*/
static status_t
smb_close_dir(fs_volume*, fs_vnode* dir, void* cookie)
{
	TRACE("URL=%s", to_smb(dir)->URL().String());
	return to_smb(dir)->CloseDir(cookie);
}


/*!	fs_vnode_ops::read_dir

	Read next one or more dir entries
	Max number of entries to read in num, return number actually read
	Must fill in dirent fields: d_dev, d_ino, d_name, d_reclen
	When end of directory has already been reached, return num=0 and B_OK
	If buffer too small for even a single netry, return B_BUFFER_OVERFLOW
	Should contain ".", ".."
*/
static status_t
smb_read_dir(fs_volume*, fs_vnode* vnode, void* cookie, struct dirent* buffer,
	size_t bufferSize, uint32* num)
{
	TRACE("URL=%s", to_smb(vnode)->URL().String());
	return to_smb(vnode)->ReadDir(cookie, buffer, bufferSize, num);
}


/*!	fs_vnode_ops::free_dir_cookie

	Delete directory cookie
*/
static status_t
smb_free_dir_cookie(fs_volume*, fs_vnode* vnode, void* cookie)
{
	TRACE("URL=%s", to_smb(vnode)->URL().String());
	return to_smb(vnode)->FreeDirCookie(cookie);
}


/*!	fs_vnode_ops::rewind_dir

	Reset directory cookie to first dir entry
*/
static status_t
smb_rewind_dir(fs_volume*, fs_vnode* vnode, void* cookie)
{
	TRACE("URL=%s", to_smb(vnode)->URL().String());
	return to_smb(vnode)->RewindDirCookie(cookie);
}


/*!	fs_vnode_ops::create_dir

	Create directory
*/
static status_t
smb_create_dir(fs_volume*, fs_vnode* parent, const char* name,
	int permissions)
{
	return to_smb(parent)->CreateDir(name, permissions);
}


/*!	fs_vnode_ops::remove_dir

	Remove directory
	Fails if directory not empty
*/
static status_t
smb_remove_dir(fs_volume*, fs_vnode* parent, const char* name)
{
	return to_smb(parent)->RemoveDir(name);
}


// #pragma mark - Operation vectors


fs_volume_ops gSmbVolumeOps = {
	&smb_unmount,
	&smb_read_fs_info,
	NULL, // write_fs_info
	NULL, // sync
	&smb_get_vnode,

	// index operations
	NULL, // open_index_dir
	NULL, // close_index_dir
	NULL, // free_index_dir
	NULL, // read_index_dir
	NULL, // rewind_index_dir
	NULL, // create_index
	NULL, // remove_index
	NULL, // read_index_stat

	// query operations
	NULL, // open_query
	NULL, // close_query
	NULL, // free_query_cookie
	NULL, // read_query
	NULL, // rewind_query

	// FS layer support
	NULL, // all_layers_mounted
	NULL, // create_sub_vnode
	NULL  // delete_sub_vnode
};


fs_vnode_ops gSmbVnodeOps = {
	// vnode operations
	&smb_lookup,
	&smb_get_vnode_name,
		// TODO: find out why userlandfs crashes when this optional
		//       hook is missing
	&smb_put_vnode,
	&smb_remove_vnode,

	// VM file access (deprecated)
	NULL, // can_page
	NULL, // read_pages
	NULL, // write_pages

	// asynchronous I/O (not implemented)
	NULL, // io
	NULL, // cancel_io

	// cache file access (not implemented)
	NULL, // get_file_map

	// common operations
	NULL, // ioctl
	NULL, // set_flags
	NULL, // select
	NULL, // deselect
	NULL, // fsync

	NULL, // read_symlink
	NULL, // create_symlink

	NULL, // link
	&smb_unlink,
	&smb_rename,

	&smb_access,
	&smb_read_stat,
	&smb_write_stat,
	NULL, // preallocate

	// file operations
	&smb_create,
	&smb_open,
	&smb_close,
	&smb_free_cookie,
	&smb_read,
	&smb_write,

	// directory operations
	&smb_create_dir,
	&smb_remove_dir,
	&smb_open_dir,
	&smb_close_dir,
	&smb_free_dir_cookie,
	&smb_read_dir,
	&smb_rewind_dir,

	// attribute directory operations
	NULL, // open_attr_dir
	NULL, // close_attr_dir
	NULL, // free_attr_dir_cookie
	NULL, // read_attr_dir
	NULL, // rewind_attr_dir

	// attribute operations
	NULL, // create_attr
	NULL, // open_attr
	NULL, // close_attr
	NULL, // free_attr_cookie
	NULL, // read_attr
	NULL, // write_attr
	NULL, // read_attr_stat
	NULL, // write_attr_stat
	NULL, // rename_attr
	NULL, // remove_attr

	// node/FS layer support
	NULL, // create_special_node
	NULL, // get_super_vnode

	// lock operations
	NULL, // test_lock
	NULL, // acquire_lock
	NULL  // release_lock
};


static file_system_module_info sSmbFileSystem = {
	{
		"file_systems/SMB-FS" B_CURRENT_FS_API_VERSION,
		0,
		NULL
	},
	"SMB-FS",
	"Server Message Block",
	0, // B_DISK_SYSTEM_SUPPORTS_WRITING,

	// scanning
	NULL, // identify_partition
	NULL, // scan_partition
	NULL, // free_identify_partition_cookie
	NULL, // free_partition_content_cookie

	// general operations
	&smb_mount,

	// capability query
	NULL, // get_supported_operations

	NULL, // validate_resize
	NULL, // validate_move
	NULL, // validate_set_content_name
	NULL, // validate_set_content_parameters
	NULL, // validate_initialize

	// shadow partition
	NULL, // shadow_changed

	// writing
	NULL, // defragment
	NULL, // repair
	NULL, // resize
	NULL, // move
	NULL, // set_content_name
	NULL, // set_content_parameter
	NULL, // initialize
	NULL  // uninitialize
};


module_info* modules[] = {
	(module_info*)&sSmbFileSystem,
	NULL
};

/*
 *  linux/fs/minix/namei.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linuxmt/types.h>
#include <linuxmt/sched.h>
#include <linuxmt/elksfs_fs.h>
#include <linuxmt/kernel.h>
#include <linuxmt/string.h>
#include <linuxmt/stat.h>
#include <linuxmt/fcntl.h>
#include <linuxmt/errno.h>
#include <linuxmt/debug.h>

#include <arch/segment.h>

/*
 * comment out this line if you want names > info->s_namelen chars to be
 * truncated. Else they will be disallowed (ENAMETOOLONG).
 */
/* #define NO_TRUNCATE */

static int namecompare(len,maxlen,name,buffer)
int len;
int maxlen;
char * name;
register char * buffer;
{
	int retval;

	if (len > maxlen)
		return 0;
	retval = !fs_memcmp(name, buffer, len);
	if (buffer[len] != 0) retval = 0;
	return retval;
}

/*
 * ok, we cannot use strncmp, as the name is not in our data space.
 * Thus we'll have to use minix_match. No big problem. Match also makes
 * some sanity tests.
 *
 * NOTE! unlike strncmp, minix_match returns 1 for success, 0 for failure.
 *
 * Note2: bh must already be mapped! 
 */
static int elksfs_match(len,name,bh,offset,info)
int len;
char * name;
struct buffer_head * bh;
unsigned long * offset;
register struct elksfs_sb_info * info;
{
	register struct elksfs_dir_entry * de;
	int retval;

	de = (struct elksfs_dir_entry *) (bh->b_data + *offset);
	*offset += info->s_dirsize;
	if (!de->inode || len > info->s_namelen) {
		return 0;
	}
	/* "" means "." ---> so paths like "/usr/lib//libc.a" work */
	if (!len && (de->name[0]=='.') && (de->name[1]=='\0')) {
		return 1;
	}
	retval = namecompare(len,(int)info->s_namelen,name,de->name);
	return retval;
}

/*
 *	elksfs_find_entry()
 *
 * finds an entry in the specified directory with the wanted name. It
 * returns the cache buffer in which the entry was found, and the entry
 * itself (as a parameter - res_dir). It does NOT read the inode of the
 * entry - you'll have to do that yourself if you want to.
 * 
 */
 
static struct buffer_head * elksfs_find_entry(dir,name,namelen,res_dir)
register struct inode * dir;
char * name;
int namelen;
struct elksfs_dir_entry ** res_dir;
{
	register unsigned long block, offset;
	struct buffer_head * bh;
	struct elksfs_sb_info * info;

	*res_dir = NULL;
	if (!dir || !dir->i_sb)
		return NULL;
	info = &dir->i_sb->u.elksfs_sb;
	if (namelen > info->s_namelen) {
#ifdef NO_TRUNCATE
		return NULL;
#else
		namelen = info->s_namelen;
#endif
	}
	bh = NULL;
	block = offset = 0;
	while (block*BLOCK_SIZE+offset < dir->i_size) {
		if (!bh) {
			bh = elksfs_bread(dir,block,0);
			if (!bh) {
				block++;
				continue;
			}
		}
		map_buffer(bh);
		*res_dir = (struct elksfs_dir_entry *) (bh->b_data + offset);
			
		if (elksfs_match(namelen,name,bh,&offset,info)) {
			unmap_buffer(bh);
			return bh;
		}
		unmap_buffer(bh);
#ifdef BLOAT_FS
		if (offset < bh->b_size)
			continue;
#else
		if (offset < 1024)
			continue;
#endif
		brelse(bh);
		bh = NULL;
		offset = 0;
		block++;
	}
	brelse(bh);
	*res_dir = NULL;
	return NULL;
}

int elksfs_lookup(dir,name,len,result)
register struct inode * dir;
char * name;
int len;
register struct inode ** result;
{
	unsigned int ino;
	struct elksfs_dir_entry * de;
	struct buffer_head * bh;

	*result = NULL;
	
	if (!dir)
		return -ENOENT;

	if (!S_ISDIR(dir->i_mode)) {
		iput(dir);
		return -ENOENT;
	}
	printd_namei("elksfs_lookup: Entering elksfs_find_entry\n");
	bh = elksfs_find_entry(dir,name,len,&de); 
	printd_namei2("elksfs_lookup: elksfs_find_entry returned %x %d\n", bh, bh->b_mapcount);
	if (!bh)
	{
		iput(dir);
		return -ENOENT;
	}
	map_buffer(bh);
	ino = de->inode;
	unmap_brelse(bh);
	*result = iget(dir->i_sb,(long) ino);
	if (!*result) {
		iput(dir);
		return -EACCES;
	}
	iput(dir);
	return 0;
}

/*
 *	elksfs_add_entry()
 *
 * adds a file entry to the specified directory, returning a possible
 * error value if it fails.
 *
 * NOTE!! The inode part of 'de' is left at 0 - which means you
 * may not sleep between calling this and putting something into
 * the entry, as someone else might have used it while you slept.
 */
 
#ifndef CONFIG_FS_RO
static int elksfs_add_entry(dir,name,namelen,res_buf,res_dir)
register struct inode * dir;
char * name;
int namelen;
struct buffer_head ** res_buf;
struct elksfs_dir_entry ** res_dir;
{
	int i;
	unsigned long block, offset;
	struct buffer_head * bh;
	struct elksfs_dir_entry * de;
	struct elksfs_sb_info * info;

	*res_buf = NULL;
	*res_dir = NULL;
	if (!dir || !dir->i_sb)
		return -ENOENT;
	info = &dir->i_sb->u.elksfs_sb;
	if (namelen > info->s_namelen) {
#ifdef NO_TRUNCATE
		return -ENAMETOOLONG;
#else
		namelen = info->s_namelen;
#endif
	}
	if (!namelen)
		return -ENOENT;
	bh = NULL;
	block = offset = 0;
	while (1) {
		if (!bh) {
			bh = elksfs_bread(dir,block,1);
			if (!bh)
				return -ENOSPC;
		}
		map_buffer(bh);
		de = (struct elksfs_dir_entry *) (bh->b_data + offset);
		offset += info->s_dirsize;
#ifdef BLOAT_FS
		if (block*bh->b_size + offset > dir->i_size) {
#else
		if (block*1024 + offset > dir->i_size) {
#endif
			de->inode = 0;
#ifdef BLOAT_FS
			dir->i_size = block*bh->b_size + offset;
#else
			dir->i_size = block*1024 + offset;
#endif
			dir->i_dirt = 1;
		}
		if (de->inode) {
			if (namecompare(namelen, (int)info->s_namelen, name, de->name)) {
				printd_mfs2("ELKSFSadd_entry: file %t==%s (already exists)\n", name, de->name);
				unmap_brelse(bh);
				return -EEXIST;
			}
		} else {
			dir->i_mtime = dir->i_ctime = CURRENT_TIME;
			dir->i_dirt = 1;
			for (i = 0; i < info->s_namelen ; i++)
				de->name[i] = (i < namelen) ? get_fs_byte(name + i) : 0;
#ifdef BLOAT_FS
			dir->i_version = ++event;
#endif
			unmap_buffer(bh);
			mark_buffer_dirty(bh, 1);
			*res_dir = de;
			break;
		}
#ifdef BLOAT_FS
		if (offset < bh->b_size)
			continue;
#else
		if (offset < 1024)
			continue;
#endif
		printk("elksfs_add_entry may need another unmap_buffer :)");
		brelse(bh);
		bh = NULL;
		offset = 0;
		block++;
	}
	*res_buf = bh;
	return 0;
}

int elksfs_create(dir,name,len,mode,result)
register struct inode * dir;
char * name;
int len;
int mode;
struct inode ** result;
{
	int error;
	register struct inode * inode;
	struct buffer_head * bh;
	struct elksfs_dir_entry * de;

	*result = NULL;
	if (!dir)
		return -ENOENT;
	inode = elksfs_new_inode(dir);
	if (!inode) {
		iput(dir);
		return -ENOSPC;
	}
	inode->i_op = &elksfs_file_inode_operations;
	inode->i_mode = mode;
	inode->i_dirt = 1;
	error = elksfs_add_entry(dir,name,len, &bh ,&de);
	if (error) {
		inode->i_nlink--;
		inode->i_dirt = 1;
		iput(inode);
		iput(dir);
		return error;
	}
	de->inode = inode->i_ino;
	mark_buffer_dirty(bh, 1);
	brelse(bh);
	iput(dir);
	*result = inode;
	return 0;
}

int elksfs_mknod(dir,name,len,mode,rdev)
register struct inode * dir;
char * name;
int len;
int mode;
int rdev;
{
	int error;
	register struct inode * inode;
	struct buffer_head * bh;
	struct elksfs_dir_entry * de;

	if (!dir)
		return -ENOENT;
	bh = elksfs_find_entry(dir,name,len,&de);
	if (bh) {
		brelse(bh);
		iput(dir);
		return -EEXIST;
	}
	inode = elksfs_new_inode(dir);
	if (!inode) {
		iput(dir);
		return -ENOSPC;
	}
	inode->i_uid = current->euid;
	inode->i_mode = mode;
	inode->i_op = NULL;
	if (S_ISREG(inode->i_mode))
		inode->i_op = &elksfs_file_inode_operations;
	else if (S_ISDIR(inode->i_mode)) {
		inode->i_op = &elksfs_dir_inode_operations;
		if (dir->i_mode & S_ISGID)
			inode->i_mode |= S_ISGID;
	}
	else if (S_ISLNK(inode->i_mode))
		inode->i_op = &elksfs_symlink_inode_operations;
	else if (S_ISCHR(inode->i_mode))
		inode->i_op = &chrdev_inode_operations;
	else if (S_ISBLK(inode->i_mode))
		inode->i_op = &blkdev_inode_operations;
#ifdef NOT_YET		
	else if (S_ISFIFO(inode->i_mode))
		init_fifo(inode);
#endif		
	if (S_ISBLK(mode) || S_ISCHR(mode))
		inode->i_rdev = to_kdev_t(rdev);
	inode->i_dirt = 1;
	error = elksfs_add_entry(dir, name, len, &bh, &de);
	if (error) {
		inode->i_nlink--;
		inode->i_dirt = 1;
		iput(inode);
		iput(dir);
		return error;
	}
	de->inode = inode->i_ino;
	mark_buffer_dirty(bh, 1);
	brelse(bh);
	iput(dir);
	iput(inode);
	return 0;
}

int elksfs_mkdir(dir,name,len,mode)
register struct inode * dir;
char * name;
int len;
int mode;
{
	int error;
	register struct inode * inode;
	struct buffer_head * dir_block;
	struct buffer_head * bh;
	struct elksfs_dir_entry * de;
	struct elksfs_sb_info * info;

	if (!dir || !dir->i_sb) {
		iput(dir);
		return -EINVAL;
	}
	info = &dir->i_sb->u.elksfs_sb;
	bh = elksfs_find_entry(dir,name,len,&de);
	if (bh) {
		brelse(bh);
		iput(dir);
		return -EEXIST;
	}
/*	map_buffer(bh); */ /* Above checks if bh is returned and exits, so bh
			    * bh is NULL at this point */
	if (dir->i_nlink >= ELKSFS_LINK_MAX) {
		iput(dir);
		return -EMLINK;
	}
	inode = elksfs_new_inode(dir);
	if (!inode) {
		iput(dir);
		return -ENOSPC;
	}
	printd_fsmkdir("m_mkdir: new_inode succeeded\n");
	inode->i_op = &elksfs_dir_inode_operations;
	inode->i_size = 2 * info->s_dirsize;
	printd_fsmkdir("m_mkdir: starting elksfs_bread\n");
	dir_block = elksfs_bread(inode,0,1);
	if (!dir_block) {
		iput(dir);
		inode->i_nlink--;
		inode->i_dirt = 1;
		iput(inode);
		return -ENOSPC;
	}
	printd_fsmkdir("m_mkdir: read succeeded\n");
	map_buffer(dir_block);
	de = (struct elksfs_dir_entry *) dir_block->b_data;
	de->inode=inode->i_ino;
	strcpy(de->name,".");
	de = (struct elksfs_dir_entry *) (dir_block->b_data + info->s_dirsize);
	de->inode = dir->i_ino;
	strcpy(de->name,"..");
	inode->i_nlink = 2;
	mark_buffer_dirty(dir_block, 1);
	unmap_brelse(dir_block);
	printd_fsmkdir("m_mkdir: dir_block update succeeded\n");
	inode->i_mode = S_IFDIR | (mode & 0777 & ~current->fs.umask);
	if (dir->i_mode & S_ISGID)
		inode->i_mode |= S_ISGID;
	inode->i_dirt = 1;
	error = elksfs_add_entry(dir, name, len, &bh, &de);
	if (error) {
		iput(dir);
		inode->i_nlink=0;
		iput(inode);
		return error;
	}
	map_buffer(bh);
	de->inode = inode->i_ino;
	mark_buffer_dirty(bh, 1);
	dir->i_nlink++;
	dir->i_dirt = 1;
	iput(dir);
	iput(inode);
	unmap_brelse(bh);
	printd_fsmkdir("m_mkdir: done!\n");
	return 0;
}

/*
 * routine to check that the specified directory is empty (for rmdir)
 */
static int empty_dir(inode)
register struct inode * inode;
{
	unsigned long block;
	unsigned int offset;
	struct buffer_head * bh;
	struct elksfs_dir_entry * de;
	struct elksfs_sb_info * info;

	if (!inode || !inode->i_sb)
		return 1;
	info = &inode->i_sb->u.elksfs_sb;
	block = 0;
	bh = NULL;
	offset = 2*info->s_dirsize;
	if (inode->i_size & (info->s_dirsize-1))
		goto bad_dir;
	if (inode->i_size < offset)
		goto bad_dir;
	bh = elksfs_bread(inode,0,0);
	if (!bh)
		goto bad_dir;
	map_buffer(bh);
	de = (struct elksfs_dir_entry *) bh->b_data;
	if (!de->inode || strcmp(de->name,"."))
		goto bad_dir;
	de = (struct elksfs_dir_entry *) (bh->b_data + info->s_dirsize);
	if (!de->inode || strcmp(de->name,".."))
		goto bad_dir;
	while (block*BLOCK_SIZE+offset < inode->i_size) {
		if (!bh) {
			bh = elksfs_bread(inode,block,0);
			if (!bh) {
				block++;
				continue;
			}
		}
		de = (struct elksfs_dir_entry *) (bh->b_data + offset);
		offset += info->s_dirsize;
		if (de->inode) {
			unmap_brelse(bh);
			return 0;
		}
#ifdef BLOAT_FS
		if (offset < bh->b_size)
			continue;
#else
		if (offset < 1024)
			continue;
#endif
		unmap_brelse(bh);
		bh = NULL;
		offset = 0;
		block++;
	}
	brelse(bh);
	return 1;
bad_dir:
	unmap_brelse(bh);
	printk("Bad directory on device %s\n",
	       kdevname(inode->i_dev));
	return 1;
}

int elksfs_rmdir(dir,name,len)
register struct inode * dir;
char * name;
int len;
{
	int retval;
	register struct inode * inode;
	struct buffer_head * bh;
	struct elksfs_dir_entry * de;

	inode = NULL;
	bh = elksfs_find_entry(dir,name,len,&de);
	retval = -ENOENT;
	if (!bh)
		goto end_rmdir;
	map_buffer(bh);
	retval = -EPERM;
	if (!(inode = iget(dir->i_sb, (long) de->inode)))
		goto end_rmdir;
        if ((dir->i_mode & S_ISVTX) && !suser() &&
            current->euid != inode->i_uid &&
            current->euid != dir->i_uid)
		goto end_rmdir;
	if (inode->i_dev != dir->i_dev)
		goto end_rmdir;
	if (inode == dir)	/* we may not delete ".", but "../dir" is ok */
		goto end_rmdir;
	if (!S_ISDIR(inode->i_mode)) {
		retval = -ENOTDIR;
		goto end_rmdir;
	}
	if (!empty_dir(inode)) {
		retval = -ENOTEMPTY;
		goto end_rmdir;
	}
	if (de->inode != inode->i_ino) {
		retval = -ENOENT;
		goto end_rmdir;
	}
	if (inode->i_count > 1) {
		retval = -EBUSY;
		goto end_rmdir;
	}
	if (inode->i_nlink != 2)
		printk("empty directory has nlink!=2 (%d)\n",inode->i_nlink);
	de->inode = 0;
#ifdef BLOAT_FS
	dir->i_version = ++event;
#endif
	mark_buffer_dirty(bh, 1);
	inode->i_nlink=0;
	inode->i_dirt=1;
	inode->i_ctime = dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	dir->i_nlink--;
	dir->i_dirt=1;
	retval = 0;
end_rmdir:
	iput(dir);
	iput(inode);
	unmap_brelse(bh);
	return retval;
}

int elksfs_unlink(dir,name,len)
struct inode * dir;
char * name;
int len;
{
	int retval;
	register struct inode * inode;
	struct buffer_head * bh;
	struct elksfs_dir_entry * de;

repeat:
	retval = -ENOENT;
	inode = NULL;
	bh = elksfs_find_entry(dir,name,len,&de);
	if (!bh)
		goto end_unlink;
	map_buffer(bh);
	if (!(inode = iget(dir->i_sb, (long) de->inode)))
		goto end_unlink;
	retval = -EPERM;
	if (S_ISDIR(inode->i_mode))
		goto end_unlink;
	if (de->inode != inode->i_ino) {
		iput(inode);
		unmap_brelse(bh);
		current->counter = 0;
		schedule();
		goto repeat;
	}
	if ((dir->i_mode & S_ISVTX) && !suser() &&
	    current->euid != inode->i_uid &&
	    current->euid != dir->i_uid)
		goto end_unlink;
	if (de->inode != inode->i_ino) {
		retval = -ENOENT;
		goto end_unlink;
	}
	if (!inode->i_nlink) {
		printk("Deleting nonexistent file (%s:%lu), %d\n",
			kdevname(inode->i_dev),
		       inode->i_ino, inode->i_nlink);
		inode->i_nlink=1;
	}
	de->inode = 0;
#ifdef BLOAT_FS
	dir->i_version = ++event;
#endif
	mark_buffer_dirty(bh, 1);
	dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	dir->i_dirt = 1;
	inode->i_nlink--;
	inode->i_ctime = dir->i_ctime;
	inode->i_dirt = 1;
	retval = 0;
end_unlink:
	unmap_brelse(bh);
	iput(inode);
	iput(dir);
	return retval;
}

int elksfs_symlink(dir,name,len,symname)
struct inode * dir;
char * name;
int len;
char * symname;
{
	struct elksfs_dir_entry * de;
	register struct inode * inode = NULL;
	struct buffer_head * bh = NULL;
	register struct buffer_head * name_block = NULL;
	int i;
	char c;

	if (!(inode = elksfs_new_inode(dir))) {
		iput(dir);
		return -ENOSPC;
	}
	inode->i_mode = S_IFLNK | 0777;
	inode->i_op = &elksfs_symlink_inode_operations;
	name_block = elksfs_bread(inode,0,1);
	if (!name_block) {
		iput(dir);
		inode->i_nlink--;
		inode->i_dirt = 1;
		iput(inode);
		return -ENOSPC;
	}
	map_buffer(name_block);
	i = 0;
	while (i < 1023 && (c=*(symname++)))
		name_block->b_data[i++] = c;
	name_block->b_data[i] = 0;
	mark_buffer_dirty(name_block, 1);
	unmap_brelse(name_block);
	inode->i_size = i;
	inode->i_dirt = 1;
	bh = elksfs_find_entry(dir,name,len,&de);
	map_buffer(bh);
	if (bh) {
		inode->i_nlink--;
		inode->i_dirt = 1;
		iput(inode);
		unmap_brelse(bh);
		iput(dir);
		return -EEXIST;
	}
	i = elksfs_add_entry(dir, name, len, &bh, &de);
	if (i) {
		inode->i_nlink--;
		inode->i_dirt = 1;
		iput(inode);
		iput(dir);
		return i;
	}
	de->inode = inode->i_ino;
	mark_buffer_dirty(bh, 1);
	brelse(bh);
	iput(dir);
	iput(inode);
	return 0;
}

int elksfs_link(oldinode,dir,name,len)
register struct inode * oldinode;
register struct inode * dir;
char * name;
int len;
{
	int error;
	struct elksfs_dir_entry * de;
	struct buffer_head * bh;

	if (S_ISDIR(oldinode->i_mode)) {
		iput(oldinode);
		iput(dir);
		return -EPERM;
	}
	if (oldinode->i_nlink >= ELKSFS_LINK_MAX) {
		iput(oldinode);
		iput(dir);
		return -EMLINK;
	}
	bh = elksfs_find_entry(dir,name,len,&de);
	if (bh) {
		brelse(bh);
		iput(dir);
		iput(oldinode);
		return -EEXIST;
	}
	error = elksfs_add_entry(dir, name, len, &bh, &de);
	if (error) {
		iput(dir);
		iput(oldinode);
		return error;
	}
	de->inode = oldinode->i_ino;
	mark_buffer_dirty(bh, 1);
	brelse(bh);
	iput(dir);
	oldinode->i_nlink++;
	oldinode->i_ctime = CURRENT_TIME;
	oldinode->i_dirt = 1;
	iput(oldinode);
	return 0;
}
#endif /* CONFIG_FS_RO */
#if 0 /* subdir() used in do_minix_rename() which is not present */
static int subdir(new_inode,old_inode)
register struct inode * new_inode;
register struct inode * old_inode;
{
	int ino;
	int result;

	new_inode->i_count++;
	result = 0;
	for (;;) {
		if (new_inode == old_inode) {
			result = 1;
			break;
		}
		if (new_inode->i_dev != old_inode->i_dev)
			break;
		ino = new_inode->i_ino;
		if (elksfs_lookup(new_inode,"..",2,&new_inode))
			break;
		if (new_inode->i_ino == ino)
			break;
	}
	iput(new_inode);
	return result;
}
#endif /* 0 */

#ifndef _LINUXMT_FS_H
#define _LINUXMT_FS_H

/*
 * This file has definitions for some important file table
 * structures etc.
 */

#include <linuxmt/wait.h>
#include <linuxmt/types.h>
#include <linuxmt/vfs.h>
#include <linuxmt/kdev_t.h>
#include <linuxmt/ioctl.h>
#include <linuxmt/pipe_fs_i.h>
#include <linuxmt/net.h>

#include <arch/bitops.h>

/*
 * It's silly to have NR_OPEN bigger than NR_FILE, but I'll fix
 * that later. Anyway, now the file code is no longer dependent
 * on bitmaps in unsigned longs, but uses the new fd_set structure..
 *
 */
 
#define NR_OPEN 	20

#define NR_INODE	96	/* this should be bigger than NR_FILE */
#define NR_FILE 	64	/* this can well be larger on a larger system */
#define NR_SUPER	4
#define NR_BUFFERS	64	/* This may be assumed by some code! */
#define NR_MAPBUFS	8	/* Maximum number of mappable buffers */	
#define BLOCK_SIZE 1024
#define BLOCK_SIZE_BITS 10

#define NR_IHASH	11

#define MAY_EXEC 1
#define MAY_WRITE 2
#define MAY_READ 4

#define FMODE_READ	1
#define FMODE_WRITE	2

#define READ 0
#define WRITE 1
#define READA 2		/* read-ahead - don't pause */
#define WRITEA 3	/* "write-ahead" - silly, but somewhat useful */

#define NIL_FILP	((struct file *)0)
#define SEL_IN		1
#define SEL_OUT		2
#define SEL_EX		4

/*
 *	Passed to namei
 */
 
#define IS_DIR	1
#define NOT_DIR	2

/*
 * These are the fs-independent mount-flags: up to 16 flags are supported
 */

#define MS_RDONLY	 1 /* mount read-only */
#define MS_NOSUID	 2 /* ignore suid and sgid bits */
#define MS_NODEV	 4 /* disallow access to device special files */
#define MS_NOEXEC	 8 /* disallow program execution */
#define MS_SYNCHRONOUS	16 /* writes are synced at once */
#define MS_REMOUNT	32 /* alter flags of a mounted FS */

#define S_APPEND    256 /* append-only file */
#define S_IMMUTABLE 512 /* immutable file */

/*
 * Flags that can be altered by MS_REMOUNT
 */
#define MS_RMT_MASK (MS_RDONLY)

/*
 * Executable formats
 */

#define EXEC_MINIX	0x1
#define EXEC_MSDOS	0x2

/*
 * Note that read-only etc flags are inode-specific: setting some file-system
 * flags just means all the inodes inherit those flags by default. It might be
 * possible to override it selectively if you really wanted to with some
 * ioctl() that is not currently implemented.
 *
 * Exception: MS_RDONLY is always applied to the entire file system.
 */

#define IS_RDONLY(inode) (((inode)->i_sb) && ((inode)->i_sb->s_flags & MS_RDONLY))
#define IS_NOSUID(inode) ((inode)->i_flags & MS_NOSUID)
#define IS_NODEV(inode) ((inode)->i_flags & MS_NODEV)
#define IS_NOEXEC(inode) ((inode)->i_flags & MS_NOEXEC)
#define IS_SYNC(inode) ((inode)->i_flags & MS_SYNCHRONOUS)

#define IS_APPEND(inode) ((inode)->i_flags & S_APPEND)
#define IS_IMMUTABLE(inode) ((inode)->i_flags & S_IMMUTABLE)

/* the read-only stuff doesn't really belong here, but any other place is
   probably as bad and I don't want to create yet another include file. */

#define BLKROSET   _IO(0x12,93)	/* set device read-only (0 = read-write) */
#define BLKROGET   _IO(0x12,94)	/* get read-only status (0 = read_write) */
#define BLKRRPART  _IO(0x12,95)	/* re-read partition table */
#define BLKGETSIZE _IO(0x12,96)	/* return device size */
#define BLKFLSBUF  _IO(0x12,97)	/* flush buffer cache */
#define BLKRASET   _IO(0x12,98)	/* Set read ahead for block device */
#define BLKRAGET   _IO(0x12,99)	/* get current read ahead setting */

#define BMAP_IOCTL 1		/* obsolete - kept for compatibility */
#define FIBMAP	   _IO(0x00,1)	/* bmap access */
#define FIGETBSZ   _IO(0x00,2)	/* get the block size used for bmap */

#ifdef __KERNEL__
extern void buffer_init();
extern void inode_init();
extern void file_table_init();

typedef char buffer_block[BLOCK_SIZE];

struct buffer_head
{
	unsigned char b_num;	/* Used to lookup L2 area */
	unsigned long b_blocknr;
	kdev_t b_dev;
	struct buffer_head *b_next;
#ifdef BLOAT_FS
	unsigned long b_state;
#endif
	struct buffer_head *b_next_lru;
	unsigned int b_count;
#ifdef BLOAT_FS
	unsigned int b_size;
#endif
	unsigned int b_mapcount; /* Used for the new L2 buffer cache scheme */
	char *b_data;		/* Address in L1 buffer area */
#ifdef BLOAT_FS
	unsigned int b_list;
	unsigned long b_flushtime;
	unsigned long b_lru_time;
#endif
	struct wait_queue *b_wait;
#ifdef BLOAT_FS
	struct buffer_head *b_prev, *b_prev_lru;
#else
	struct buffer_head *b_prev_lru;
#endif
	char b_uptodate, b_dirty, b_lock;
	
};

#define BLOCK_READ	0
#define BLOCK_WRITE	1

#define mark_buffer_dirty(bh, st) ((bh)->b_dirty = (st))
#define mark_buffer_clean(bh) ((bh)->b_dirty = 0)
#define buffer_dirty(bh) ((bh)->b_dirty)
#define buffer_clean(bh) (!(bh)->b_dirty)
#define buffer_uptodate(bh) ((bh)->b_uptodate)
#define buffer_locked(bh) ((bh)->b_lock)

#define ll_rw_block(rw,nr,bh) ll_rw_blk(rw,bh)
	
extern void brelse();
extern void bforget();
extern struct inode *iget();

/*
 * Attribute flags.  These should be or-ed together to figure out what
 * has been changed!
 */

#define ATTR_MODE	1
#define ATTR_UID	2
#define ATTR_GID	4
#define ATTR_SIZE	8
#define ATTR_ATIME	16
#define ATTR_MTIME	32
#define ATTR_CTIME	64
#define ATTR_ATIME_SET	128
#define ATTR_MTIME_SET	256
#define ATTR_FORCE	512

/*
 * This is the Inode Attributes structure, used for notify_change().  It
 * uses the above definitions as flags, to know which values have changed.
 * Also, in this manner, a Filesystem can look at only the values it cares
 * about.  Basically, these are the attributes that the VFS layer can
 * request to change from the FS layer.
 *
 * Derek Atkins <warlord@MIT.EDU> 94-10-20
 */
struct iattr 
{
	unsigned int	ia_valid;
	umode_t		ia_mode;
	uid_t		ia_uid;
	gid_t		ia_gid;
	off_t		ia_size;
	time_t		ia_atime;
	time_t		ia_mtime;
	time_t		ia_ctime;
};

#include <linuxmt/romfs_fs_i.h>

struct inode 
{
	/* This stuff is on disk */
	__u16	i_mode;
	__u16	i_uid;
	__u32	i_size;
	__u32	i_mtime;
	__u8	i_gid;
	__u8	i_nlink;
	__u16	i_zone[9];
	/* This stuff is just in-memory... */
	unsigned long	i_ino;
	kdev_t		i_dev;
	kdev_t		i_rdev;
	time_t		i_atime;
	time_t		i_ctime;
	unsigned long	i_blksize;
#ifdef BLOAT_FS
	unsigned long	i_blocks;
	unsigned long	i_version;
	struct file_lock * i_flock;
#endif
/*	struct semaphore i_sem;*/
	struct inode_operations * i_op;
	struct super_block * i_sb;
	struct wait_queue * i_wait;
	struct inode * i_next, * i_prev;
	struct inode * i_hash_next, *i_hash_prev;
	struct inode * i_mount;
	unsigned short i_count;
#ifdef BLOAT_FS
	struct inode * i_bound_to, * i_bound_by;
	unsigned short i_wcount;
#endif
	unsigned short i_flags;
	unsigned char i_lock;
	unsigned char i_dirt;
#ifdef CONFIG_PIPE
	unsigned char i_pipe;
#endif
	unsigned char i_sock;
#ifdef BLOAT_FS
	unsigned char i_seek;
	unsigned char i_update;
#endif
	union {
		struct pipe_inode_info pipe_i;
		struct romfs_inode_info romfs_i;
		struct socket socket_i;
		void *generic_i;
	} u;
};

struct file {
	mode_t f_mode;
	loff_t f_pos;
	unsigned short f_flags;
	unsigned short f_count;
	struct inode * f_inode;
	struct file_operations * f_op;
#ifdef BLOAT_FS
	off_t f_reada;
	unsigned long f_version;
	void *private_data;	/* needed for tty driver, but not ntty */
#endif
};

#include <linuxmt/minix_fs_sb.h>
#include <linuxmt/romfs_fs_sb.h>
#include <linuxmt/elksfs_fs_sb.h>

struct super_block {
	kdev_t s_dev;
	unsigned char s_lock;
#ifdef BLOAT_FS
	unsigned char s_rd_only;
#endif
	unsigned char s_dirt;
	struct file_system_type *s_type;
	struct super_operations *s_op;
	unsigned int s_flags;
	unsigned long s_magic;
#ifdef BLOAT_FS
	unsigned long s_time;
#endif
	struct inode * s_covered;
	struct inode * s_mounted;
	struct wait_queue * s_wait;
	union {
		struct minix_sb_info minix_sb;
		struct romfs_sb_info romfs_sb;
		struct elksfs_sb_info elksfs_sb;
		void *generic_sbp;
	} u;
};

/*
 * This is the "filldir" function type, used by readdir() to let
 * the kernel specify what kind of dirent layout it wants to have.
 * This allows the kernel to read directories into kernel space or
 * to have different dirent layouts depending on the binary type.
 */

typedef int (*filldir_t)();
	
struct file_operations {
	int (*lseek) ();
	int (*read) ();
	int (*write) ();
	int (*readdir) ();
	int (*select) ();
	int (*ioctl) ();
	int (*open) ();
	void (*release) ();
#ifdef BLOAT_FS
	int (*fsync) ();
	int (*check_media_change) ();
	int (*revalidate) ();
#endif
};

struct inode_operations {
	struct file_operations * default_file_ops;
	int (*create) ();
	int (*lookup) ();
	int (*link) ();
	int (*unlink) ();
	int (*symlink) ();
	int (*mkdir) ();
	int (*rmdir) ();
	int (*mknod) ();
	int (*readlink) ();
	int (*follow_link) ();
#ifdef BLOAT_FS
	int (*bmap) ();
#endif
	void (*truncate) ();
#ifdef BLOAT_FS
	int (*permission) ();
#endif
};

struct super_operations {
	void (*read_inode) ();
#ifdef BLOAT_FS
	int (*notify_change) ();
#endif
	void (*write_inode) ();
	void (*put_inode) ();
	void (*put_super) ();
	void (*write_super) ();
#ifdef BLOAT_FS
	void (*statfs_kern) ();	/* i8086 statfs goes to kernel, then user */
#endif
	int (*remount_fs) ();
};

struct file_system_type {
	struct super_block *(*read_super) ();
	char *name;
#ifdef BLOAT_FS
	int requires_dev;
#endif
};

extern int event;		/* Event counter */

extern int register_filesystem();
extern int unregister_filesystem();

extern int sys_open();
extern int sys_close();		/* yes, it's really unsigned */
extern void _close_allfiles();

extern int getname();
extern void putname();

extern int register_blkdev();
extern int unregister_blkdev();
extern int blkdev_open();
extern struct file_operations def_blk_fops;
extern struct inode_operations blkdev_inode_operations;

extern int register_chrdev();
extern int unregister_chrdev();
extern int chrdev_open();
extern struct file_operations def_chr_fops;
extern struct inode_operations chrdev_inode_operations;

extern void init_fifo();

extern struct file_operations connecting_fifo_fops;
extern struct file_operations read_fifo_fops;
extern struct file_operations write_fifo_fops;
extern struct file_operations rdwr_fifo_fops;
extern struct file_operations read_pipe_fops;
extern struct file_operations write_pipe_fops;
extern struct file_operations rdwr_pipe_fops;

extern struct file_system_type *get_fs_type();

extern int fs_may_mount();
extern int fs_may_umount();
extern int fs_may_remount_ro();

extern struct file file_array[];
extern int nr_files;
extern struct super_block super_blocks[];

extern void set_writetime();

extern struct buffer_head ** buffer_pages;
extern int nr_buffers;
extern int buffermem;
extern int nr_buffer_heads;
extern int blksize_size[];

#define BUF_CLEAN 0
#define BUF_UNSHARED 1 /* Buffers that were shared but are not any more */
#define BUF_LOCKED 2   /* Buffers scheduled for write */
#define BUF_LOCKED1 3  /* Supers, inodes */
#define BUF_DIRTY 4    /* Dirty buffers, not yet scheduled for write */
#define BUF_SHARED 5   /* Buffers shared */
#define NR_LIST 6

extern void invalidate_inodes();
extern void invalidate_buffers();
extern int floppy_is_wp();
extern void sync_inodes();
extern void sync_dev();
extern void fsync_dev();
extern void sync_supers();
extern int bmap();
extern int notify_change();
extern int namei();
extern int lnamei();
extern int permission();
#ifdef BLOAT_FS
extern int get_write_access();
extern void put_write_access();
#else 
#define get_write_access(_a)
#define put_write_access(_a)
#endif
extern int open_namei();
extern int do_mknod();
extern int do_pipe();
extern void iput();
extern struct inode * __iget();
extern struct inode * get_empty_inode();
extern void insert_inode_hash();
extern void clear_inode();
extern struct inode * get_pipe_inode();
extern struct file * get_empty_filp();
extern int close_fp();
extern struct buffer_head * get_hash_table();
extern struct buffer_head * getblk();
extern struct buffer_head * readbuf();
extern void ll_rw_blk();
extern void ll_rw_page();
extern void ll_rw_swap_file();
extern int is_read_only();
extern void buffer_wait();
extern void buffer_wait_clean();
extern struct buffer_head *buffer_get();
#define BG_NOREAD 	1
#define PRI_DISKIO	1
#define PRI_SWAPIO	0
extern void buffer_free();
extern void buffer_lock();
extern void buffer_unlock();

extern void map_buffer();
extern void unmap_buffer();
extern void unmap_brelse();
extern void print_bufmap_status();

extern void put_super();
extern kdev_t ROOT_DEV;

extern void show_buffers();
extern void mount_root();

extern int char_read();
extern int block_read();
extern int read_ahead[];

extern int char_write();
extern int block_write();

extern int block_fsync();
extern int file_fsync();

extern int inode_change_ok();
extern void inode_setattr();

#endif /* __KERNEL__ */

#endif

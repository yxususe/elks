/*
 * A tiny read-only filesystem in memory
 * Build filesystem image from directory
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

/* TODO: move list primitives to library */
#include "list.h"


/* Super block in memory */

#define SUPER_MAGIC "ROMFS"

struct super_disk_s
	{
	byte_t magic [6];
	word_t ssize;    /* size of super block */
	word_t isize;    /* size of inode */
	word_t icount;   /* number of inodes */
	};

typedef struct super_disk_s super_disk_t;


/* INode on disk (actually in ROM) */

#define INODE_FILE  0x0000
#define INODE_DIR   0x0001
#define INODE_CHAR  0x0002
#define INODE_BLOCK 0x0003

struct inode_disk_s
	{
	word_t offset;  /* offset in paragraphs */
	word_t size;    /* size in bytes */
	word_t flags;
	};

typedef struct inode_disk_s inode_disk_t;


/* INode to build */

struct inode_build_s
	{
	list_node_t node;
	list_root_t entries;  /* list of entries for directory */
	char * path;
	word_t index;

	word_t dev;           /* major & minor for device */
	word_t flags;
	addr_t offset;
	addr_t size;
	};

typedef struct inode_build_s inode_build_t;

static list_root_t _inodes;  /* list of inodes */


static inode_build_t * inode_alloc (inode_build_t * parent, char * path)
	{
	inode_build_t * inode;

	while (1)
		{
		inode = (inode_build_t *) malloc (sizeof (inode_build_t));
		assert (inode);

		list_init (&inode->entries);

		inode->path = strdup (path);
		assert (inode->path);

		list_add_tail (&_inodes, &inode->node);
		inode->index = _inodes.count;  /* 0 = no inode */
		break;
		}

	return inode;
	}


static void inode_free (inode_build_t * inode)
	{
	if (inode->path) free (inode->path);
	/* TODO: empty entries */
	}


/* Entry to build */

struct entry_build_s
	{
	list_node_t node;
	inode_build_t * inode;
	char * name;
	};

typedef struct entry_build_s entry_build_t;


static entry_build_t * entry_alloc (inode_build_t * inode, char * name)
	{
	entry_build_t * entry;

	while (1)
		{
		entry = (entry_build_t *) malloc (sizeof (entry_build_t));
		assert (entry);

		entry->name = strdup (name);
		assert (entry->name);

		list_add_tail (&inode->entries, &entry->node);
		break;
		}

	return entry;
	}


static void entry_free (entry_build_t * entry)
	{
	if (entry->name) free (entry->name);
	}


/*---------------------------------------------------------------------------*/
/* Recursive directory parsing                                               */
/*---------------------------------------------------------------------------*/

static int parse_dir (inode_build_t * grand_parent_inode,
	inode_build_t * parent_inode, char * parent_path)
	{
	int err;

	DIR * parent_dir = NULL;

	while (1)
		{
		parent_dir = opendir (parent_path);
		if (!parent_dir)
			{
			err = errno;
			break;
			}

		while (1)
			{
			struct dirent * ent = readdir (parent_dir);
			if (!ent)
				{
				err = errno;
				break;
				}

			char * name = ent->d_name;
			entry_build_t * child_ent = entry_alloc (parent_inode, name);
			if (!child_ent)
				{
				err = -1;
				break;
				}

			char * child_path = malloc (strlen (parent_path) + 1 + strlen (name) + 1);
			strcpy (child_path, parent_path);
			strcat (child_path, "/");
			strcat (child_path, name);

			if (!strcmp (name, "."))
				{
				printf ("Pseudo: %s\n", child_path);
				child_ent->inode = parent_inode;
				}

			else if (!strcmp (name, ".."))
				{
				printf ("Pseudo: %s\n", child_path);
				if (grand_parent_inode)
					child_ent->inode = grand_parent_inode;
					else
					child_ent->inode = parent_inode;  /* self for root */

				}

			else
				{
				inode_build_t * child_inode = inode_alloc (parent_inode, child_path);
				if (!child_inode)
					{
					err = -1;
					break;
					}

				child_ent->inode = child_inode;

				struct stat child_stat;
				err = lstat (child_path, &child_stat);
				if (err)
					{
					err = errno;
					break;
					}

				mode_t mode = child_stat.st_mode;

				if (S_ISREG (mode))
					{
					printf ("File:   %s\n", child_path);
					child_inode->flags = INODE_FILE;
					child_inode->size = child_stat.st_size;
					}

				else if (S_ISDIR (mode))
					{
					printf ("Dir:    %s\n", child_path);
					child_inode->flags = INODE_DIR;
					err = parse_dir (parent_inode, child_inode, child_path);
					if (err) break;
					}

				else if (S_ISCHR (mode))
					{
					printf ("Char:   %s\n", child_path);
					child_inode->flags = INODE_CHAR;
					goto char_block_inode;
					}

				else if (S_ISBLK (mode))
					{
					printf ("Block:  %s\n", child_path);
					child_inode->flags = INODE_BLOCK;

					char_block_inode:
					child_inode->dev = child_stat.st_rdev;
					child_inode->size = 0;
					}

				else
					{
					/* Unsupported inode type */
					assert (0);
					}
				}

			if (child_path) free (child_path);
			}

		if (err != 0)
			{
			perror ("Read directory");
			}

		break;
		}

	if (parent_dir) closedir (parent_dir);

	return err;
	}


/*---------------------------------------------------------------------------*/
/* File system compilation                                                   */
/*---------------------------------------------------------------------------*/

#define BLOCK_SIZE 256

static int compile_file (int fdout, inode_build_t * inode)
	{
	int err;

	int fdin = -1;

	while (1)
		{
		fdin = open (inode->path, O_RDONLY);
		if (fdin < 0)
			{
			err = errno;
			break;
			}

		word_t size = 0;

		while (1)
			{
			byte_t buf [BLOCK_SIZE];

			int count_in = read (fdin, buf, BLOCK_SIZE);
			if (count_in < 0)
				{
				err = errno;
				break;
				}

			if (!count_in)
				{
				err = 0;
				break;
				}

			int count_out = write (fdout, buf, count_in);
			if (count_out != count_in)
				{
				err = 1;
				break;
				}

			size += count_out;
			}

		if (err) break;

		inode->size = size;
		break;
		}

	if (fdin >= 0) close (fdin);

	return err;
	}


static int compile_dir (int fd, inode_build_t * inode)
	{
	int err;

	while (1)
		{
		word_t size = 0;
		int count;

		entry_build_t * entry = (entry_build_t *) inode->entries.node.next;
		while (entry != (entry_build_t *) &inode->entries.node)
			{
			/* Entry inode index */

			count = write (fd, &entry->inode->index, sizeof (word_t));
			if (count != sizeof (word_t))
				{
				err = errno;
				break;
				}

			size += count;

			/* Entry string */
			/* First byte is string length */

			int len = strlen (entry->name);
			if (len >= 256)
				{
				err = 1;
				break;
				}

			count = write (fd, &len, sizeof (byte_t));
			if (count != sizeof (byte_t))
				{
				err = errno;
				break;
				}

			size += count;

			count = write (fd, entry->name, len);
			if (count != len)
				{
				err = errno;
				break;
				}

			size += count;

			entry = (entry_build_t *) entry->node.next;
			}

		inode->size = size;

		err = 0;
		break;
		}

	return err;
	}


static int compile_fs ()
	{
	int err;

	int fd = -1;

	while (1)
		{
		/* Mode is required by O_CREAT */

		int fd = open ("romfs.bin", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
		if (fd < 0)
			{
			err = errno;
			break;
			}

		/* Skip the superblock and the table of inodes */
		/* that form the first block with size <= 64 KB */
		/* and align next block to paragraph */

		off_t offset = sizeof (super_disk_t) + _inodes.count * sizeof (inode_disk_t);
		offset = (offset & 0xF) ? ((offset & 0xFFFF0) + 0x10) : offset;

		offset = lseek (fd, offset, SEEK_SET);
		if (offset < 0)
			{
			err = errno;
			break;
			}

		/* Compile the inodes (directories, files, etc) */

		inode_build_t * inode_build = (inode_build_t *) _inodes.node.next;
		while (inode_build != (inode_build_t *) &_inodes.node)
			{
			inode_build->offset = offset;

			if (inode_build->flags == INODE_DIR)
				{
				printf ("Dir:    %s\n", inode_build->path);
				err = compile_dir (fd, inode_build);
				}

			else if (inode_build->flags == INODE_FILE)
				{
				printf ("File:   %s\n", inode_build->path);
				err = compile_file (fd, inode_build);
				}

			if (err) break;

			/* Align every data block on paragraph */

			offset += inode_build->size;

			if (offset & 0xF)
				{
				offset = (offset & 0xFFFF0) + 0x10;
				offset = lseek (fd, offset, SEEK_SET);
				if (offset < 0)
					{
					err = errno;
					break;
					}
				}

			inode_build = (inode_build_t *) inode_build->node.next;
			}

		/* Now write first data block */
		/* with the SuperBlock and the table of INodes */

		offset = lseek (fd, 0, SEEK_SET);
		if (offset < 0)
			{
			err = errno;
			break;
			}

		super_disk_t super;
		memset (&super, 0, sizeof (super_disk_t));

		strcpy (super.magic, SUPER_MAGIC);
		super.ssize = sizeof (super_disk_t);
		super.isize = sizeof (inode_disk_t);
		super.icount = _inodes.count;

		int count = write (fd, &super, sizeof (super_disk_t));
		if (count != sizeof (super_disk_t))
			{
			err = errno;
			break;
			}

		inode_build = (inode_build_t *) _inodes.node.next;
		while (inode_build != (inode_build_t *) &_inodes.node)
			{
			inode_disk_t inode_disk;
			memset (&inode_disk, 0, sizeof (inode_disk_t));

			word_t flags = inode_build->flags;
			inode_disk.flags = flags;

			if (flags == INODE_FILE || flags == INODE_DIR)
				{
				/* TODO: replace assert() by error */
				assert (inode_build->size < 0x10000);
				word_t size = inode_build->size;
				inode_disk.size = size;

				if (size)
					{
					/* TODO: replace assert() by error */
					assert (inode_build->offset < 0x100000);
					assert ((inode_build->offset & 0xF) == 0);
					inode_disk.offset = (inode_build->offset >> 4);  /* in paragraphs */
					}
				}

			else if (flags == INODE_CHAR || flags == INODE_BLOCK)
				{
				/* Data block offset holds the device major & minor */
				inode_disk.offset = inode_build->dev;
				}

			else
				{
				/* Unsupported inode type */
				assert (0);
				}

			int count = write (fd, &inode_disk, sizeof (inode_disk_t));
			if (count != sizeof (inode_disk_t))
				{
				err = errno;
				break;
				}

			inode_build = (inode_build_t *) inode_build->node.next;
			}

		break;
		}

	if (fd >= 0) close (fd);

	return err;
	}


/*---------------------------------------------------------------------------*/
/* Main                                                                      */
/*---------------------------------------------------------------------------*/

void main (int argc, char ** argv)
	{
	while (1)
		{
		int err;

		if (argc != 2)
			{
			puts ("usage: mkromfs <dir>");
			err = 1;
			break;
			}

		printf ("Starting from %s directory.\n", argv [1]);

		puts ("\nParsing file system...");

		list_init (&_inodes);

		inode_build_t * root_node = inode_alloc (NULL, argv [1]);
		if (!root_node) break;

		root_node->flags = INODE_DIR;

		err = parse_dir (NULL, root_node, argv [1]);
		if (err) break;

		puts ("End of parsing.");

		printf ("\nTotal inodes: %u\n", _inodes.count);

		puts ("\nCompiling file system...");

		err = compile_fs ();
		if (err) break;

		puts ("End of compilation.");
		break;
		}
	}


/*---------------------------------------------------------------------------*/

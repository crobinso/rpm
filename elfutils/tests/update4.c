/* Test program for elf_update function.
   Copyright (C) 2000, 2001, 2002 Red Hat, Inc.
   Written by Ulrich Drepper <drepper@redhat.com>, 2000.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 as
   published by the Free Software Foundation.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <libelf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libebl.h>


int
main (int argc, char *argv[])
{
  const char fname[] = "xxx";
  int fd;
  Elf *elf;
  Elf32_Ehdr *ehdr;
  Elf32_Phdr *phdr;
  Elf_Scn *scn;
  Elf32_Shdr *shdr;
  Elf_Data *data;
  struct Ebl_Strtab *shst;
  struct Ebl_Strent *firstse;
  struct Ebl_Strent *secondse;
  struct Ebl_Strent *thirdse;
  struct Ebl_Strent *fourthse;
  struct Ebl_Strent *shstrtabse;
  int i;

  fd = open (fname, O_RDWR | O_CREAT | O_TRUNC, 0666);
  if (fd == -1)
    {
      printf ("cannot open `%s': %s\n", fname, strerror (errno));
      exit (1);
    }

  elf_version (EV_CURRENT);

  elf_fill (0x42);

  elf = elf_begin (fd, ELF_C_WRITE, NULL);
  if (elf == NULL)
    {
      printf ("cannot create ELF descriptor: %s\n", elf_errmsg (-1));
      exit (1);
    }

  /* Create an ELF header.  */
  ehdr = elf32_newehdr (elf);
  if (ehdr == NULL)
    {
      printf ("cannot create ELF header: %s\n", elf_errmsg (-1));
      exit (1);
    }

  /* Print the ELF header values.  */
  if (argc > 1)
    {
      for (i = 0; i < EI_NIDENT; ++i)
	printf (" %02x", ehdr->e_ident[i]);
      printf ("\
\ntype = %hu\nmachine = %hu\nversion = %u\nentry = %u\nphoff = %u\n"
	      "shoff = %u\nflags = %u\nehsize = %hu\nphentsize = %hu\n"
	      "phnum = %hu\nshentsize = %hu\nshnum = %hu\nshstrndx = %hu\n",
	      ehdr->e_type, ehdr->e_machine, ehdr->e_version, ehdr->e_entry,
	      ehdr->e_phoff, ehdr->e_shoff, ehdr->e_flags, ehdr->e_ehsize,
	      ehdr->e_phentsize, ehdr->e_phnum, ehdr->e_shentsize,
	      ehdr->e_shnum, ehdr->e_shstrndx);
    }

  ehdr->e_ident[0] = 42;
  ehdr->e_ident[4] = 1;
  ehdr->e_ident[5] = 1;
  ehdr->e_ident[6] = 2;
  ehdr->e_type = ET_EXEC;
  ehdr->e_version = 1;
  ehdr->e_ehsize = 1;
  elf_flagehdr (elf, ELF_C_SET, ELF_F_DIRTY);

  /* Create the program header.  */
  phdr = elf32_newphdr (elf, 1);
  if (phdr == NULL)
    {
      printf ("cannot create program header: %s\n", elf_errmsg (-1));
      exit (1);
    }

  phdr[0].p_type = PT_PHDR;
  elf_flagphdr (elf, ELF_C_SET, ELF_F_DIRTY);

  shst = ebl_strtabinit (true);

  scn = elf_newscn (elf);
  if (scn == NULL)
    {
      printf ("cannot create first section: %s\n", elf_errmsg (-1));
      exit (1);
    }
  shdr = elf32_getshdr (scn);
  if (shdr == NULL)
    {
      printf ("cannot get header for first section: %s\n", elf_errmsg (-1));
      exit (1);
    }

  firstse = ebl_strtabadd (shst, ".first", 0);

  shdr->sh_type = SHT_PROGBITS;
  shdr->sh_flags = SHF_ALLOC | SHF_EXECINSTR;
  shdr->sh_addr = 0;
  shdr->sh_link = 0;
  shdr->sh_info = 0;
  shdr->sh_entsize = 1;

  data = elf_newdata (scn);
  if (data == NULL)
    {
      printf ("cannot create data first section: %s\n", elf_errmsg (-1));
      exit (1);
    }

  data->d_buf = "hello";
  data->d_type = ELF_T_BYTE;
  data->d_version = EV_CURRENT;
  data->d_size = 5;
  data->d_align = 16;


  scn = elf_newscn (elf);
  if (scn == NULL)
    {
      printf ("cannot create second section: %s\n", elf_errmsg (-1));
      exit (1);
    }
  shdr = elf32_getshdr (scn);
  if (shdr == NULL)
    {
      printf ("cannot get header for second section: %s\n", elf_errmsg (-1));
      exit (1);
    }

  secondse = ebl_strtabadd (shst, ".second", 0);

  shdr->sh_type = SHT_PROGBITS;
  shdr->sh_flags = SHF_ALLOC | SHF_WRITE;
  shdr->sh_addr = 0;
  shdr->sh_link = 0;
  shdr->sh_info = 0;
  shdr->sh_entsize = 1;

  data = elf_newdata (scn);
  if (data == NULL)
    {
      printf ("cannot create data second section: %s\n", elf_errmsg (-1));
      exit (1);
    }

  data->d_buf = "world";
  data->d_type = ELF_T_BYTE;
  data->d_version = EV_CURRENT;
  data->d_size = 5;
  data->d_align = 16;


  scn = elf_newscn (elf);
  if (scn == NULL)
    {
      printf ("cannot create third section: %s\n", elf_errmsg (-1));
      exit (1);
    }
  shdr = elf32_getshdr (scn);
  if (shdr == NULL)
    {
      printf ("cannot get header for third section: %s\n", elf_errmsg (-1));
      exit (1);
    }

  thirdse = ebl_strtabadd (shst, ".third", 0);

  shdr->sh_type = SHT_PROGBITS;
  shdr->sh_flags = SHF_ALLOC | SHF_EXECINSTR;
  shdr->sh_addr = 0;
  shdr->sh_link = 0;
  shdr->sh_info = 0;
  shdr->sh_entsize = 1;

  data = elf_newdata (scn);
  if (data == NULL)
    {
      printf ("cannot create data third section: %s\n", elf_errmsg (-1));
      exit (1);
    }

  data->d_buf = "!!!!!";
  data->d_type = ELF_T_BYTE;
  data->d_version = EV_CURRENT;
  data->d_size = 5;
  data->d_align = 16;


  scn = elf_newscn (elf);
  if (scn == NULL)
    {
      printf ("cannot create fourth section: %s\n", elf_errmsg (-1));
      exit (1);
    }
  shdr = elf32_getshdr (scn);
  if (shdr == NULL)
    {
      printf ("cannot get header for fourth section: %s\n", elf_errmsg (-1));
      exit (1);
    }

  fourthse = ebl_strtabadd (shst, ".fourth", 0);

  shdr->sh_type = SHT_NOBITS;
  shdr->sh_flags = SHF_ALLOC | SHF_EXECINSTR;
  shdr->sh_addr = 0;
  shdr->sh_link = 0;
  shdr->sh_info = 0;
  shdr->sh_entsize = 1;
  shdr->sh_size = 100;

  data = elf_newdata (scn);
  if (data == NULL)
    {
      printf ("cannot create data fourth section: %s\n", elf_errmsg (-1));
      exit (1);
    }

  data->d_buf = NULL;
  data->d_type = ELF_T_BYTE;
  data->d_version = EV_CURRENT;
  data->d_size = 100;
  data->d_align = 16;


  scn = elf_newscn (elf);
  if (scn == NULL)
    {
      printf ("cannot create SHSTRTAB section: %s\n", elf_errmsg (-1));
      exit (1);
    }
  shdr = elf32_getshdr (scn);
  if (shdr == NULL)
    {
      printf ("cannot get header for SHSTRTAB section: %s\n", elf_errmsg (-1));
      exit (1);
    }

  shstrtabse = ebl_strtabadd (shst, ".shstrtab", 0);

  shdr->sh_type = SHT_STRTAB;
  shdr->sh_flags = 0;
  shdr->sh_addr = 0;
  shdr->sh_link = SHN_UNDEF;
  shdr->sh_info = SHN_UNDEF;
  shdr->sh_entsize = 1;

  /* We have to store the section index in the ELF header.  */
  ehdr->e_shstrndx = elf_ndxscn (scn);

  data = elf_newdata (scn);
  if (data == NULL)
    {
      printf ("cannot create data SHSTRTAB section: %s\n", elf_errmsg (-1));
      exit (1);
    }

  /* No more sections, finalize the section header string table.  */
  ebl_strtabfinalize (shst, data);

  elf32_getshdr (elf_getscn (elf, 1))->sh_name = ebl_strtaboffset (firstse);
  elf32_getshdr (elf_getscn (elf, 2))->sh_name = ebl_strtaboffset (secondse);
  elf32_getshdr (elf_getscn (elf, 3))->sh_name = ebl_strtaboffset (thirdse);
  elf32_getshdr (elf_getscn (elf, 4))->sh_name = ebl_strtaboffset (fourthse);
  shdr->sh_name = ebl_strtaboffset (shstrtabse);

  /* Let the library compute the internal structure information.  */
  if (elf_update (elf, ELF_C_NULL) < 0)
    {
      printf ("failure in elf_update(NULL): %s\n", elf_errmsg (-1));
      exit (1);
    }

  ehdr = elf32_getehdr (elf);

  phdr[0].p_offset = ehdr->e_phoff;
  phdr[0].p_offset = ehdr->e_phoff;
  phdr[0].p_vaddr = ehdr->e_phoff;
  phdr[0].p_paddr = ehdr->e_phoff;
  phdr[0].p_flags = PF_R | PF_X;
  phdr[0].p_filesz = ehdr->e_phnum * elf32_fsize (ELF_T_PHDR, 1, EV_CURRENT);
  phdr[0].p_memsz = ehdr->e_phnum * elf32_fsize (ELF_T_PHDR, 1, EV_CURRENT);
  phdr[0].p_align = sizeof (Elf32_Word);

  /* Write out the file.  */
  if (elf_update (elf, ELF_C_WRITE) < 0)
    {
      printf ("failure in elf_update(WRITE): %s\n", elf_errmsg (-1));
      exit (1);
    }

  /* Print the ELF header values.  */
  if (argc > 1)
    {
      for (i = 0; i < EI_NIDENT; ++i)
	printf (" %02x", ehdr->e_ident[i]);
      printf ("\
\ntype = %hu\nmachine = %hu\nversion = %u\nentry = %u\nphoff = %u\n"
	      "shoff = %u\nflags = %u\nehsize = %hu\nphentsize = %hu\n"
	      "phnum = %hu\nshentsize = %hu\nshnum = %hu\nshstrndx = %hu\n",
	      ehdr->e_type, ehdr->e_machine, ehdr->e_version, ehdr->e_entry,
	      ehdr->e_phoff, ehdr->e_shoff, ehdr->e_flags, ehdr->e_ehsize,
	      ehdr->e_phentsize, ehdr->e_phnum, ehdr->e_shentsize,
	      ehdr->e_shnum, ehdr->e_shstrndx);
    }

  if (elf_end (elf) != 0)
    {
      printf ("failure in elf_end: %s\n", elf_errmsg (-1));
      exit (1);
    }

  unlink (fname);

  return 0;
}

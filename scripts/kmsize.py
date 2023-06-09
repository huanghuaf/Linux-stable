#!/usr/bin/python
#
# Display summary and details of the kernel code section size.
# Sort the objects by size in detail files.
#
# Author: Shuosheng Huang <huangshuosheng@allwinnertech.com>
#

import os
import sys
import shutil

DIR_KNM_RESULT = "./result_km"
FILE_KNM = DIR_KNM_RESULT + "/knm_info_all.txt"
FILE_SECTION_TEXT_INFO = DIR_KNM_RESULT + "/text_info.txt"
FILE_SECTION_DATA_INFO = DIR_KNM_RESULT + "/data_info.txt"
FILE_SECTION_BSS_INFO = DIR_KNM_RESULT + "/bss_info.txt"
FILE_SECTION_RODATA_INFO = DIR_KNM_RESULT + "/rodata_info.txt"
FILE_SECTION_OTHER_INFO = DIR_KNM_RESULT + "/other_info.txt"


def del_and_create_new_dir(path):
	path = path.strip()
	path = path.rstrip()
	isExists = os.path.exists(path)
	if isExists:
#		print "Delete Dir: " + path
		shutil.rmtree(path)
	os.makedirs(path)
#	print "Dir [" + path + "] Create successfully"
	return True

def get_nm_info(file_name):
	cmd_str = "nm --size -r" + " " + file_name + ">" + FILE_KNM
	os.system(cmd_str)

def dump_section_size(file_knm):
	text_len = 0
	data_len = 0
	bss_len = 0
	rodata_len = 0
	other_len = 0
	knm_fd = open(file_knm, 'r')
	for line in knm_fd:
		section_name = line[9:10]
		section_size = int(line[0:8], 16)
		if section_name == "T" or section_name == "t" :
			text_len += section_size
		elif section_name == "D" or section_name == "d" :
			data_len += section_size
		elif section_name == "B" or section_name == "b" :
			bss_len += section_size
		elif section_name == "R" or section_name == "r" :
			rodata_len += section_size
		else :
			other_len += section_size
	print(" .text   :  %-6.2f Kb" % (text_len/1024.0))
	print(" .data   :  %-6.2f Kb" % (data_len/1024.0))
	print(" .rodata :  %-6.2f Kb" % (rodata_len/1024.0))
	print(" .bss    :  %-6.2f Kb" % (bss_len/1024.0))
	print(" .other  :  %-6.2f Kb" % (other_len/1024.0))
	knm_fd.close()

def save_section_detail_info(file_knm):
	knm_fd = open(file_knm, 'r')
	text_fd = open(FILE_SECTION_TEXT_INFO , 'w')
	data_fd = open(FILE_SECTION_DATA_INFO , 'w')
	bss_fd = open(FILE_SECTION_BSS_INFO , 'w')
	rodata_fd = open(FILE_SECTION_RODATA_INFO , 'w')
	other_fd = open(FILE_SECTION_OTHER_INFO , 'w')
	print >> text_fd, ("Size(byte)       Type  Name")
	print >> data_fd, ("Size(byte)       Type  Name")
	print >> bss_fd, ("Size(byte)       Type  Name")
	print >> rodata_fd, ("Size(byte)       Type  Name")
	print >> other_fd, ("Size(byte)       Type  Name")
	for line in knm_fd:
		section_name = line[9:10]
		section_size = int(line[0:8], 16)
		line = line.strip()
		if section_name == "T" or section_name == "t" :
			print >> text_fd, ("%-8d %s" % (section_size, line))
		elif section_name == "D" or section_name == "d" :
			print >> data_fd, ("%-8d %s" % (section_size, line))
		elif section_name == "B" or section_name == "b" :
			print >> bss_fd, ("%-8d %s" % (section_size, line))
		elif section_name == "R" or section_name == "r" :
			print >> rodata_fd, ("%-8d %s" % (section_size, line))
		else :
			print >> other_fd, ("%-8d %s" % (section_size, line))
	knm_fd.close()
	text_fd.close()
	data_fd.close()
	bss_fd.close()
	rodata_fd.close()
	other_fd.close()


if len(sys.argv) != 2 :
	print "Usage: ./kmsize.py xxx.o"
	sys.exit(1)

del_and_create_new_dir(DIR_KNM_RESULT)
FILE_INPUT = sys.argv[1]
#file_path = os.getcwd() + "/" + FILE_INPUT
#print ("File name \033[1;31m %s \033[0m" % file_path)
print (" ---Section summary---")
get_nm_info(FILE_INPUT)
dump_section_size(FILE_KNM)
save_section_detail_info(FILE_KNM)
print (" For more detail, please see: \033[1;31m %s/ \033[0m" % DIR_KNM_RESULT)


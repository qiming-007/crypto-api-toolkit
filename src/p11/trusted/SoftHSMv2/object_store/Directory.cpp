/*
 * Copyright (C) 2019-2020 Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in
 *      the documentation and/or other materials provided with the
 *      distribution.
 *   3. Neither the name of Intel Corporation nor the names of its
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*
 * Copyright (c) 2010 SURFnet bv
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*****************************************************************************
 Directory.cpp

 Helper functions for accessing directories.
 *****************************************************************************/

#include "config.h"
#include "Directory.h"
#include "OSPathSep.h"
//#include "log.h"
#include <string>
#include <vector>
#ifndef SGXHSM
#ifndef _WIN32
#include <dirent.h>
#include <unistd.h>
#else
#include <direct.h>
#include <io.h>
#endif
#include <errno.h>
#include <stdio.h>
#else
#include "p11Enclave_t.h"
#include <sgx_tprotected_fs.h>
#endif
#include <string.h>
#ifndef SGXHSM
#include <sys/types.h>
#include <sys/stat.h>
#endif


// Constructor
Directory::Directory(std::string inPath)
{
	path = inPath;
	dirMutex = MutexFactory::i()->getMutex();

	valid = (dirMutex != NULL) && refresh();
}

// Destructor
Directory::~Directory()
{
	MutexFactory::i()->recycleMutex(dirMutex);
}

// Check if the directory is valid
bool Directory::isValid()
{
	return valid;
}

// Return a list of all files in a directory
std::vector<std::string> Directory::getFiles()
{
	// Make sure that no other thread is in the process of changing
	// the file list when we return it
	MutexLocker lock(dirMutex);

	return files;
}

// Return a list of all subdirectories in a directory
std::vector<std::string> Directory::getSubDirs()
{
	// Make sure that no other thread is in the process of changing
	// the subdirectory list when we return it
	MutexLocker lock(dirMutex);

	return subDirs;
}

// Refresh the directory listing
bool Directory::refresh()
{
	// Prevent concurrent call until valid is reset
	MutexLocker lock(dirMutex);

	// Reset the state
	valid = false;

	subDirs.clear();
	files.clear();

#ifndef SGXHSM
#ifndef _WIN32
	// Enumerate the directory
	DIR* dir = opendir(path.c_str());

	if (dir == NULL)
	{
		//DEBUG_MSG("Failed to open directory %s", path.c_str());

		return false;
	}

	// Enumerate the directory
	struct dirent* entry = NULL;

	while ((entry = readdir(dir)) != NULL)
	{
		bool pushed = false;

		// Check if this is the . or .. entry
		if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
		{
			continue;
		}

		// Convert the name of the entry to a C++ string
		std::string name(entry->d_name);

#if defined(_DIRENT_HAVE_D_TYPE) && defined(_BSD_SOURCE)
		// Determine the type of the entry
		switch(entry->d_type)
		{
		case DT_DIR:
			// This is a directory
			subDirs.push_back(name);
			pushed = true;
			break;
		case DT_REG:
			// This is a regular file
			files.push_back(name);
			pushed = true;
			break;
		default:
			break;
		}
#endif
		if (!pushed) {
			// The entry type has to be determined using lstat
			struct stat entryStatus;

			std::string fullPath = path + OS_PATHSEP + name;

			if (!lstat(fullPath.c_str(), &entryStatus))
			{
				if (S_ISDIR(entryStatus.st_mode))
				{
					subDirs.push_back(name);
				}
				else if (S_ISREG(entryStatus.st_mode))
				{
					files.push_back(name);
				}
				else
				{
					//DEBUG_MSG("File not used %s", name.c_str());
				}
			}
		}
	}

	// Close the directory
	closedir(dir);

#else
	// Enumerate the directory
	std::string pattern;
	intptr_t h;
	struct _finddata_t fi;

	if ((path.back() == '/') || (path.back() == '\\'))
		pattern = path + "*";
	else
		pattern = path + "/*";
	memset(&fi, 0, sizeof(fi));
	h = _findfirst(pattern.c_str(), &fi);
	if (h == -1)
	{
		// empty directory
		if (errno == ENOENT)
			goto finished;

		//DEBUG_MSG("Failed to open directory %s", path.c_str());

		return false;
	}

	// scan files & subdirs
	do {
		// Check if this is the . or .. entry
		if (!strcmp(fi.name, ".") || !strcmp(fi.name, ".."))
			continue;

		if ((fi.attrib & _A_SUBDIR) == 0)
			files.push_back(fi.name);
		else
			subDirs.push_back(fi.name);

		memset(&fi, 0, sizeof(fi));
	} while (_findnext(h, &fi) == 0);

	(void) _findclose(h);

    finished:
#endif
#else
    uint32_t        subDirsSize = 0;
    uint32_t        filesSize = 0;
    uint32_t        subDirsRequiredSize = 0;
    uint32_t        filesRequiredSize = 0;
    size_t          rv = CKR_OK;      
    std::string     subDirsString;
    std::string     filesString;
	sgx_status_t sgxStatus { SGX_ERROR_UNEXPECTED };

    sgxStatus = ocall_refresh(&rv, path.c_str(),
                              const_cast<char*>(subDirsString.data()), subDirsSize, &subDirsRequiredSize,
                              const_cast<char*>(filesString.data()), filesSize, &filesRequiredSize);

    if (SGX_SUCCESS == sgxStatus)
    {
        if (CKR_BUFFER_TOO_SMALL == rv)
        {
            if (subDirsRequiredSize)
            {
                subDirsSize = subDirsRequiredSize;
                subDirsString.resize(subDirsRequiredSize);
            }

            if (filesRequiredSize)
            {
                filesSize = filesRequiredSize;
                filesString.resize(filesRequiredSize);
            }

            sgxStatus = ocall_refresh(&rv, path.c_str(),
                                      const_cast<char*>(subDirsString.data()), subDirsSize, &subDirsRequiredSize,
                                      const_cast<char*>(filesString.data()), filesSize, &filesRequiredSize);
        }
        else if (rv != CKR_OK)
        {
            return false;
        }

        if (SGX_SUCCESS == sgxStatus)
        {
            char *str = nullptr;

            if (subDirsRequiredSize)
            {
                str = std::strtok(const_cast<char*>(subDirsString.c_str()),"*");

			    while (str)
			    {
				    subDirs.push_back(str);
				    str = std::strtok(nullptr, "*");
                }
            }

            if (filesRequiredSize)
            {
                str = std::strtok(const_cast<char*>(filesString.c_str()),"*");

                while (str)
                {
                    files.push_back(str);
                    str = std::strtok(nullptr, "*");
                }
            }
        }
        else
        {
		    valid = false;
            return false;
        }
    }
    else
    {
        valid = false;
        return false;
    }
#endif

	valid = true;
	return true;
}

// Create a new subdirectory
bool Directory::mkdir(std::string name)
{
	std::string fullPath = path + OS_PATHSEP + name;

#ifndef SGXHSM
#ifndef _WIN32
    int rv = ::mkdir(fullPath.c_str(), S_IFDIR | S_IRWXU);
#else
    int rv = _mkdir(fullPath.c_str());
#endif
	if (rv != 0)
	{
		//ERROR_MSG("Failed to create the directory (%s): %s", strerror(errno), fullPath.c_str());

		return false;
	}

	return refresh();
#else
    uint8_t rv = true;
	sgx_status_t sgxStatus = ocall_mkdir(&rv, fullPath.c_str());

    if (SGX_SUCCESS != sgxStatus)
    {
        return false;
    }

    if (!rv)
    {
        //ERROR_MSG("Failed to create the directory (%s): %s", strerror(errno), fullPath.c_str());

        return false;
    }

    return refresh();
#endif
}

// Delete a subdirectory in the directory
bool Directory::rmdir(std::string name, bool doRefresh /* = false */)
{
	std::string fullPath;

	if (name.empty())
		fullPath = path;
	else
		fullPath = path + OS_PATHSEP + name;

#ifndef SGXHSM
#ifndef _WIN32
	if (::rmdir(fullPath.c_str()) != 0)
		return false;
#else
	if (_rmdir(fullPath.c_str()) != 0)
		return false;
#endif
#else
    uint8_t rv = true;
	sgx_status_t sgxStatus = ocall_rmdir(&rv, fullPath.c_str());

    if (SGX_SUCCESS != sgxStatus)
    {
        return false;
    }

	if (!rv) 
    {
        return false;
    }

#endif
	if (doRefresh)
		return refresh();
	return true;
}

// Delete a file in the directory
bool Directory::remove(std::string name)
{
	std::string fullPath = path + OS_PATHSEP + name;
#ifndef SGXHSM
#ifndef _WIN32
	return (!::remove(fullPath.c_str()) && refresh());
#else
	return (!_unlink(fullPath.c_str()) && refresh());
#endif
#else
	return (!sgx_remove(fullPath.c_str()) && refresh());
#endif
}


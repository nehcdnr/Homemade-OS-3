#include"systemcall.h"
#include"io.h"
#include"file.h"
#include"common.h"

static_assert(sizeof(OpenFileMode) == sizeof(uintptr_t));

uintptr_t systemCall_openFile(const char *fileName, uintptr_t fileNameLength, OpenFileMode openMode){
	return systemCall4(SYSCALL_OPEN_FILE, (uintptr_t)fileName, fileNameLength, openMode.value);
}

uintptr_t syncOpenFileN(const char *fileName, uintptr_t nameLength, OpenFileMode openMode){
	uintptr_t handle;
	uintptr_t r = systemCall_openFile(fileName, nameLength, openMode);
	if(r == IO_REQUEST_FAILURE)
		return r;
	if(r != systemCall_waitIOReturn(r, 1, &handle))
		return IO_REQUEST_FAILURE;
	return handle;
}

uintptr_t syncOpenFile(const char *fileName){
	return syncOpenFileN(fileName, strlen(fileName), OPEN_FILE_MODE_0);
}

uintptr_t syncEnumerateFileN(const char *fileName, uintptr_t nameLength){
	OpenFileMode m = OPEN_FILE_MODE_0;
	m.enumeration = 1;
	return syncOpenFileN(fileName, nameLength, m);
}

uintptr_t syncEnumerateFile(const char *fileName){
	return syncEnumerateFileN(fileName, strlen(fileName));
}

uintptr_t systemCall_closeFile(uintptr_t handle){
	return systemCall2(SYSCALL_CLOSE_FILE, handle);
}

uintptr_t syncCloseFile(uintptr_t handle){
	uintptr_t r;
	r = systemCall_closeFile(handle);
	if(r == IO_REQUEST_FAILURE)
		return r;
	if(r != systemCall_waitIO(r))
		return IO_REQUEST_FAILURE;
	return handle;
}

uintptr_t systemCall_readFile(uintptr_t handle, void *buffer, uintptr_t bufferSize){
	return systemCall4(SYSCALL_READ_FILE, handle, (uintptr_t)buffer, bufferSize);
}

uintptr_t syncReadFile(uintptr_t handle, void *buffer, uintptr_t *bufferSize){
	uintptr_t r;
	r = systemCall_readFile(handle, buffer, *bufferSize);
	if(r == IO_REQUEST_FAILURE)
		return r;
	if(r != systemCall_waitIOReturn(r, 1, bufferSize))
		return IO_REQUEST_FAILURE;
	return handle;
}

uintptr_t systemCall_writeFile(uintptr_t handle, const void *buffer, uintptr_t bufferSize){
	return systemCall4(SYSCALL_WRITE_FILE, handle, (uintptr_t)buffer, bufferSize);
}

uintptr_t syncWriteFile(uintptr_t handle, const void *buffer, uintptr_t *bufferSize){
	uintptr_t r;
	r = systemCall_writeFile(handle, buffer, *bufferSize);
	if(r == IO_REQUEST_FAILURE)
		return r;
	if(r != systemCall_waitIOReturn(r, 1, bufferSize))
		return IO_REQUEST_FAILURE;
	return handle;
}

uintptr_t systemCall_seekReadFile(uintptr_t handle, void *buffer, uint64_t position, uintptr_t bufferSize){
	return systemCall6(SYSCALL_SEEK_READ_FILE, handle, (uintptr_t)buffer, bufferSize,
		LOW64(position), HIGH64(position));
}

uintptr_t syncSeekReadFile(uintptr_t handle, void *buffer, uint64_t position, uintptr_t *bufferSize){
	uintptr_t r;
	r = systemCall_seekReadFile(handle, buffer, position, *bufferSize);
	if(r == IO_REQUEST_FAILURE)
		return r;
	if(r != systemCall_waitIOReturn(r, 1, bufferSize))
		return IO_REQUEST_FAILURE;
	return handle;
}

uintptr_t systemCall_seekWriteFile(uintptr_t handle, void *buffer, uint64_t position, uintptr_t bufferSize){
	return systemCall6(SYSCALL_SEEK_READ_FILE, handle, (uintptr_t)buffer, bufferSize,
		LOW64(position), HIGH64(position));
}

uintptr_t systemCall_getFileParameter(uintptr_t handle, enum FileParameter parameterCode){
	return systemCall3(SYSCALL_GET_FILE_PARAMETER, handle, parameterCode);
}

uintptr_t syncGetFileParameter(uintptr_t handle, enum FileParameter paramCode, uint64_t *value){
	uintptr_t r, valueLow, valueHigh;
	r = systemCall_getFileParameter(handle, paramCode);
	if(r == IO_REQUEST_FAILURE)
		return r;
	if(r != systemCall_waitIOReturn(r, 2, &valueLow, &valueHigh))
		return IO_REQUEST_FAILURE;
	*value = COMBINE64(valueHigh, valueLow);
	return handle;
}

uintptr_t syncSizeOfFile(uintptr_t handle, uint64_t *size){
	return syncGetFileParameter(handle, FILE_PARAM_SIZE, size);
}

uintptr_t syncMaxWriteSizeOfFile(uintptr_t handle, uintptr_t *size){
	uint64_t size64;
	uintptr_t r = syncGetFileParameter(handle, FILE_PARAM_MAX_WRITE_SIZE, &size64);
	if(r != IO_REQUEST_FAILURE){
		*size = LOW64(size64);
	}
	return r;
}

uintptr_t syncMinReadSizeOfFile(uintptr_t handle, uintptr_t *size){
	uint64_t size64;
	uintptr_t r = syncGetFileParameter(handle, FILE_PARAM_MIN_READ_SIZE, &size64);
	if(r != IO_REQUEST_FAILURE){
		*size = LOW64(size64);
	}
	return r;
}

uintptr_t systemCall_setFileParameter(uintptr_t handle, enum FileParameter paramCode, uint64_t value){
	return systemCall5(SYSCALL_SET_FILE_PARAMETER, handle, paramCode, LOW64(value), HIGH64(value));
}

uintptr_t syncSetFileParameter(uintptr_t handle, enum FileParameter paramCode, uint64_t value){
	uintptr_t r;
	r = systemCall_setFileParameter(handle, paramCode, value);
	if(r == IO_REQUEST_FAILURE)
		return r;
	if(r != systemCall_waitIO(r))
		return IO_REQUEST_FAILURE;
	return handle;
}

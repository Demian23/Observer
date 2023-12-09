#pragma once

enum class FileNameOptions {
	Normalized = FLT_FILE_NAME_NORMALIZED,
	Opened = FLT_FILE_NAME_OPENED,
	Short = FLT_FILE_NAME_SHORT,

	QueryDefault = FLT_FILE_NAME_QUERY_DEFAULT,
	QueryCacheOnly = FLT_FILE_NAME_QUERY_CACHE_ONLY,
	QueryFileSystemOnly = FLT_FILE_NAME_QUERY_FILESYSTEM_ONLY,

	RequestFromCurrentProvider = FLT_FILE_NAME_REQUEST_FROM_CURRENT_PROVIDER,
	DoNotCache = FLT_FILE_NAME_DO_NOT_CACHE,
	AllowQueryOnReparse = FLT_FILE_NAME_ALLOW_QUERY_ON_REPARSE
};
DEFINE_ENUM_FLAG_OPERATORS(FileNameOptions);

class FileNameInfo final{
public:
	inline FileNameInfo(PFLT_CALLBACK_DATA data,
		FileNameOptions options = FileNameOptions::QueryDefault | FileNameOptions::Normalized){
		if (!NT_SUCCESS(FltGetFileNameInformation(data, (FLT_FILE_NAME_OPTIONS)options, &nameInfo)))
			nameInfo = nullptr;}
	~FileNameInfo() { if (nameInfo) FltReleaseFileNameInformation(nameInfo); }
	inline operator bool() const { return nameInfo != nullptr; };
	inline PFLT_FILE_NAME_INFORMATION operator->()const { return nameInfo; }
	inline PFLT_FILE_NAME_INFORMATION get()const { return nameInfo; }
	NTSTATUS parse() { return FltParseFileNameInformation(nameInfo); };
private:
	PFLT_FILE_NAME_INFORMATION nameInfo;
};

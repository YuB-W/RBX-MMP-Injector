#include <iostream>
#include <fstream>
#include <array>
#include <string>
#include <vector>
#include <thread>
#include <filesystem>
#include <map>
#include "Update.hpp"

#include <intrin.h>
#include <Windows.h>
#include <TlHelp32.h>
#include "oxorany_include.h"
#include <winhttp.h>



#include <cstdint>
#include <cstdio>
#include <atomic>
#include <unordered_set>
#include <random>


#pragma comment(lib, "winhttp.lib")

#pragma region SCF Constants & Utility

// colors

#define COLOR_RESET     "\033[0m"
#define COLOR_INFO      "\033[1;34m"  // [+]
#define COLOR_WARN      "\033[1;33m"  // [!]
#define COLOR_QUESTION  "\033[1;36m"  // [?]
#define COLOR_ERROR     "\033[1;31m"  // [ERROR]
#define COLOR_OK        "\033[1;32m"  // [OK]

//

using Stk_t = void**;

static std::vector<uint8_t> ReadFile(const std::string& path) {
	std::ifstream stream(path, std::ios::binary | std::ios::ate);

	if (!stream.is_open()) {
		return {};
	}

	size_t fileSize = static_cast<size_t>(stream.tellg());
	stream.seekg(0, std::ios::beg);

	std::vector<uint8_t> buffer(fileSize);

	if (!stream.read(reinterpret_cast<char*>(buffer.data()), fileSize)) {
		return {};
	}

	return buffer;
}

#define SCF_WRAP_START _Pragma("optimize(\"\", off)")
#define SCF_WRAP_END _Pragma("optimize(\"\", on)")

#define SCF_END goto __scf_skip_end;__debugbreak();__halt();__scf_skip_end:{};
#define SCF_STACK *const_cast<Stk_t*>(&__scf_ptr_stk);
#define SCF_START const Stk_t __scf_ptr_stk = reinterpret_cast<const Stk_t>(ofssetss_new::SCF_MARKER_STK); Stk_t Stack = SCF_STACK;

constexpr uint64_t ceil_div(uint64_t Number, uint64_t Divisor) {
	return Number / Divisor + (Number % Divisor > 0);
}

template<typename T = uint64_t, size_t Size, size_t Items = ceil_div(Size, sizeof(T))>
constexpr std::array<T, Items> to_integer(const char(&Str)[Size]) {
	std::array<T, Items> result = { 0 };

	for (size_t i = 0; i < Size; ++i) {
		result[i / sizeof(T)] |= static_cast<T>(Str[i]) << (8 * (i % sizeof(T)));
	}

	return result;
}

#define STK_STRING(Name, String)										\
constexpr auto _buf_##Name = to_integer<uint64_t>(String);					\
const char* ##Name = reinterpret_cast<const char*>(&_buf_##Name);

template<typename RetType, typename ...Args>
struct SelfContained {
	union {
		void* Page = nullptr;
		RetType(*Function)(Args...); /* used for LOCAL testing */
	};
	size_t Size = 0;

	void* HData = nullptr;
	HANDLE Target = INVALID_HANDLE_VALUE;

	SelfContained() = default;
	SelfContained(void* Page, size_t Size) : Page(Page), Size(Size) {}
	SelfContained(uintptr_t Page, size_t Size) : Page(reinterpret_cast<void*>(Page)), Size(Size) {}
};

struct FunctionData {
	void* Page;
	size_t Size;
};
#pragma endregion

#define Offset(Base, Length) reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(Base) + Length)

class Exception : public std::runtime_error {
public:
	Exception(const std::string& Message)
		: std::runtime_error(std::format("{} failed with: {}", Message, GetLastError()))
	{
	}
	Exception(const std::string& Message, const std::string& Detail)
		: std::runtime_error(std::format("{} failed with: {}", Message, Detail))
	{
	}
};

namespace Process {
	struct Module {
		uint32_t Size = 0;
		uintptr_t Start = 0;
		uintptr_t End = 0;
		HANDLE Target = INVALID_HANDLE_VALUE;
		std::string Name = "";
		std::map<std::string, void*> Exports = {};

		__forceinline void* GetAddress(std::string Name) {
			if (!Exports.contains(Name)) {
				return nullptr;
			}
			return Exports[Name];
		}
	};

	namespace details {
#pragma region Memory Utility
		template<typename T = void*, typename AddrType = void*>
		__forceinline T RemoteAlloc(HANDLE Handle, size_t Size = sizeof(T), uint32_t ProtectionType = PAGE_READWRITE, uint32_t AllocationType = MEM_COMMIT | MEM_RESERVE) {
			void* Address = VirtualAllocEx(Handle, nullptr, Size, AllocationType, ProtectionType);

			if (!Address) {
				throw Exception(oxorany("VirtualAllocEx"));
			}

			return reinterpret_cast<T>(Address);
		}

		template<typename AddrType = void*>
		__forceinline void RemoteFree(HANDLE Handle, AddrType Address, size_t Size = 0, uint32_t FreeType = MEM_RELEASE) {
			bool Success = VirtualFreeEx(Handle, Address, Size, FreeType);
			if (!Success) {
				throw Exception(oxorany("VirtualFreeEx"));
			}
		}

		template<typename T = void*, typename AddrType = void*>
		__forceinline void RemoteWrite(HANDLE Handle, AddrType Address, T Buffer, size_t Size = sizeof(T)) {
			size_t Count = 0;
			bool Success = WriteProcessMemory(Handle, reinterpret_cast<void*>(Address), Buffer, Size, &Count);

			if (!Success) {
				throw Exception(oxorany("WriteProcessMemory"));
			}

			if (Count != Size) {
				throw Exception(oxorany("WriteProcessMemory"), oxorany("Partial write"));
			}
		}

		template<typename AddrType = void*>
		__forceinline uint32_t RemoteProtect(HANDLE Handle, AddrType Address, size_t Size, uint32_t ProtectionType, bool* StatusOut = nullptr) {
			DWORD OriginalProtection = 0;
			bool Success = VirtualProtectEx(Handle, (void*)Address, Size, ProtectionType, &OriginalProtection);

			if (StatusOut) {
				*StatusOut = Success;
			}
			else if (!Success) {
				throw Exception(oxorany("VirtualAllocEx"));
			}

			return OriginalProtection;
		}

		template<typename T, typename AddrType = void*>
		__forceinline T RemoteRead(HANDLE Handle, AddrType Address, size_t Size = sizeof(T)) {
			void* Buffer = std::malloc(Size);

			if (!Buffer) {
				throw std::bad_alloc();
			}

			size_t Count = 0;
			bool Success = ReadProcessMemory(Handle, reinterpret_cast<void*>(Address), Buffer, Size, &Count);

			if (!Success) {
				throw Exception(oxorany("ReadProcessMemory"));
			}

			if (Count != Size) {
				throw Exception(oxorany("ReadProcessMemory"), oxorany("Partial read"));
			}

			T Result = {};
			std::memcpy(&Result, Buffer, Size);
			std::free(Buffer);
			return Result;
		}

		template<typename T, typename AddrType = void*>
		__forceinline void RemoteRead(HANDLE Handle, AddrType Address, T* Buffer, size_t Size = sizeof(T)) {
			size_t Count = 0;
			bool Success = ReadProcessMemory(Handle, reinterpret_cast<void*>(Address), Buffer, Size, &Count);

			if (!Success) {
				throw Exception(oxorany("ReadProcessMemory"));
			}

			if (Count != Size) {
				throw Exception(oxorany("ReadProcessMemory"), oxorany("Partial read"));
			}
		}

		template<typename AddrType = void*>
		__forceinline std::string ReadString(HANDLE Handle, AddrType Address, size_t Length = 0) {
			std::string Result = {};
			Result.resize(Length);

			uintptr_t Current = reinterpret_cast<uintptr_t>(Address);
			if (Length == 0) {
				char TempBuffer[16] = {};
				while (true) {
					if (Result.size() > 10000) {
						throw Exception(oxorany("ReadString"), oxorany("Possible infinite loop"));
					}

					RemoteRead(Handle, Current, TempBuffer, sizeof(TempBuffer));
					Current += sizeof(TempBuffer);

					size_t Len = strnlen(TempBuffer, 16);
					Result.append(TempBuffer, Len);

					if (Len != 16) {
						break;
					}
				}
			}
			else {
				char* TempBuffer = new char[Length];
				RemoteRead(Handle, Current, TempBuffer, Length);
				Result.assign(TempBuffer, Length);
				delete[] TempBuffer;
			}

			return Result;
		}
#pragma endregion

#pragma region Process & Module Utility
		static HANDLE OpenSnapshot(uint32_t Flags, uint32_t Id, int maxRetries = 20) {
			HANDLE Snapshot = CreateToolhelp32Snapshot(Flags, Id);
			int retryCount = 0;

			while (Snapshot == INVALID_HANDLE_VALUE) {
				DWORD lastError = GetLastError();
				if (lastError == ERROR_ACCESS_DENIED || lastError == ERROR_INVALID_PARAMETER) {
					std::cerr << oxorany("Snapshot failed: ") << lastError << std::endl;
					return INVALID_HANDLE_VALUE;
				}

				if (lastError == ERROR_BAD_LENGTH && Flags == TH32CS_SNAPMODULE || Flags == TH32CS_SNAPMODULE32) {
					Snapshot = CreateToolhelp32Snapshot(Flags, Id);
					continue;
				}

				std::cerr << oxorany("Snapshot failed: ") << lastError << oxorany(". Retrying");

				if (++retryCount >= maxRetries) {
					std::cerr << oxorany("Max Retrys") << std::endl;
					return INVALID_HANDLE_VALUE;
				}

				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				Snapshot = CreateToolhelp32Snapshot(Flags, Id);
			}

			return Snapshot;
		}

		static uint32_t _FindProcessByName(std::wstring Name) {
			uint32_t HighestCount = 0;
			uint32_t ProcessId = 0;

			HANDLE Snapshot = OpenSnapshot(TH32CS_SNAPPROCESS, 0);

			PROCESSENTRY32W Entry = {};
			Entry.dwSize = sizeof(Entry);

			if (!Process32FirstW(Snapshot, &Entry)) {
				CloseHandle(Snapshot);
				throw std::runtime_error(oxorany("Failed to find first Process."));
			}

			do {
				if (Name == std::wstring(Entry.szExeFile) && Entry.cntThreads > HighestCount) {
					HighestCount = Entry.cntThreads;
					ProcessId = Entry.th32ProcessID;
				}
			} while (Process32NextW(Snapshot, &Entry));


			CloseHandle(Snapshot);
			return ProcessId;
		}

		static void UpdateExports(Module& Data) {
			void* Base = (void*)Data.Start;
			HANDLE Handle = Data.Target;

			if (Base == nullptr) {
				return;
			}

			IMAGE_DOS_HEADER DosHeader = details::RemoteRead<IMAGE_DOS_HEADER>(Handle, Base);

			if (DosHeader.e_magic != IMAGE_DOS_SIGNATURE) {
				throw Exception(oxorany("UpdateExports"), oxorany("Invalid DosHeader"));
			}

			IMAGE_NT_HEADERS64 NtHeaders = RemoteRead<IMAGE_NT_HEADERS64>(Handle, Offset(Base, DosHeader.e_lfanew));
			IMAGE_DATA_DIRECTORY ExportDataDirectory = NtHeaders.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
			if (!ExportDataDirectory.VirtualAddress) {
				return;
			}
			if (!ExportDataDirectory.Size) {
				return;
			}
			IMAGE_EXPORT_DIRECTORY ExportDirectory = RemoteRead<IMAGE_EXPORT_DIRECTORY>(Handle, Offset(Base, ExportDataDirectory.VirtualAddress));

			DWORD NumberOfNames = ExportDirectory.NumberOfNames;
			DWORD NumberOfFunctions = ExportDirectory.NumberOfFunctions;

			void* AddressOfFunctions = Offset(Base, ExportDirectory.AddressOfFunctions);
			void* AddressOfNames = Offset(Base, ExportDirectory.AddressOfNames);
			void* AddressOfNameOrdinals = Offset(Base, ExportDirectory.AddressOfNameOrdinals);

			std::vector<DWORD> NameRVAs = {};
			NameRVAs.resize(NumberOfNames);
			RemoteRead<DWORD>(Handle, AddressOfNames, NameRVAs.data(), NumberOfNames * sizeof(DWORD));

			std::vector<WORD> OrdinalsRVAs = {};
			OrdinalsRVAs.resize(NumberOfNames);
			RemoteRead<WORD>(Handle, AddressOfNameOrdinals, OrdinalsRVAs.data(), NumberOfNames * sizeof(WORD));

			std::vector<DWORD> FunctionRVAs = {};
			FunctionRVAs.resize(NumberOfFunctions);
			RemoteRead<DWORD>(Handle, AddressOfFunctions, FunctionRVAs.data(), NumberOfFunctions * sizeof(DWORD));

			size_t Index = 0;
			for (DWORD NameRVA : NameRVAs) {
				std::string NameString = ReadString(Handle, Offset(Base, NameRVA));
				WORD NameOrdinal = OrdinalsRVAs[Index];
				Data.Exports[NameString] = Offset(Base, FunctionRVAs[NameOrdinal]);
				Index++;
			}
		};

		static bool _FindModule(std::string Name, Module& Data, uint32_t Id, HANDLE Handle) {
			HANDLE Snapshot = OpenSnapshot(TH32CS_SNAPMODULE, Id);

			MODULEENTRY32 Entry = {};
			Entry.dwSize = sizeof(Entry);

			if (!Module32First(Snapshot, &Entry)) {
				CloseHandle(Snapshot);
				throw std::runtime_error(oxorany("Failed to find first Module."));
			}

			do {
				if (Entry.th32ProcessID != Id) {
					continue;
				}

				std::filesystem::path Path(Entry.szExePath);

				if (Name == Path.filename().string()) {
					Data.Name = Name;
					Data.Size = Entry.modBaseSize;
					Data.Target = Handle;
					Data.Start = reinterpret_cast<uintptr_t>(Entry.modBaseAddr);
					Data.End = Data.Start + Data.Size;
					UpdateExports(Data);
					CloseHandle(Snapshot);
					return true;
				}
			} while (Module32Next(Snapshot, &Entry));

			CloseHandle(Snapshot);
			return false;
		}

		Module _WaitForModule(std::string Name, uint32_t Id, HANDLE Handle) {
			Module Data = {};

			while (!_FindModule(Name, Data, Id, Handle)) {}

			return Data;
		}

		static uint32_t _WaitForProcess(std::wstring Name) {
			uint32_t ProcessId = 0;
			while (!ProcessId) {
				try {
					ProcessId = _FindProcessByName(Name);
				}
				catch (const std::runtime_error& ex) {
					std::cerr << oxorany("FindProcess Exception: ") << ex.what() << std::endl;
				}

				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
			return ProcessId;
		}
#pragma endregion
	}

	struct Object {
		HANDLE _handle = INVALID_HANDLE_VALUE;
		uint32_t _id = 0;

		Module GetModule(std::string Name) const {
			return details::_WaitForModule(Name, _id, _handle);
		}
	};

	static Object WaitForProcess(const std::wstring& Name) {
		uint32_t Id = details::_WaitForProcess(Name);
		HANDLE Handle = OpenProcess(PROCESS_ALL_ACCESS, false, Id);

		return Object{
			._handle = Handle,
			._id = Id
		};
	}
}

namespace Injector {
	namespace details {
		template<typename T>
		__forceinline T LocalRead(const uint8_t* Bytes) {
			return *reinterpret_cast<T*>(const_cast<uint8_t*>(Bytes));
		}

		template<typename T>
		__forceinline void LocalWrite(const uint8_t* Bytes, T Value) {
			*reinterpret_cast<T*>(const_cast<uint8_t*>(Bytes)) = Value;
		}

		static __forceinline const size_t CalculateFunctionSize(void* Function) {
			uint8_t* Bytes = reinterpret_cast<uint8_t*>(Function);
			size_t Size = 0;

			while (LocalRead<uint32_t>(Bytes + Size) != ofssetss_new::SCF_END_MARKER) {
				Size++;
			}

			const size_t kSize = Size;

			while (Size - kSize < 16) {
				switch (LocalRead<uint8_t>(Bytes + Size)) {
				case 0xCC: {
					if (Size == kSize + 3) {
						goto return_size;
					}
					break;
				}
				case 0xC2: {
					Size += 3;
					goto return_size;
				}
				case 0xC3: {
					Size++;
					goto return_size;
				}
				}

				Size++;
			}

		return_size:
			return Size;
		}

		static __forceinline const size_t CalculateStackSize(const std::vector<void*>& StackPointers, const size_t FunctionSize) {
			uintptr_t StackStart = FunctionSize + sizeof(void*);
			uintptr_t AlignedStackStart = StackStart + (StackStart % sizeof(void*));

			uintptr_t StackEnd = AlignedStackStart + (StackPointers.size() * sizeof(void*));

			return StackEnd - StackStart;
		}

		static __forceinline void* ReadJmpRel32(Process::Object& proc, void* Instruction) {
			int32_t RelativeOffset = Process::details::RemoteRead<int32_t>(proc._handle, Offset(Instruction, 1));
			return Offset(Offset(Instruction, 5), RelativeOffset);
		}

		static __forceinline void* ReadJmpM64(Process::Object& proc, void* Instruction) {
			return Process::details::RemoteRead<void*>(proc._handle, Offset(Instruction, 6));
		}

		static __forceinline void* WriteJmpM64(Process::Object& proc, void* Instruction, void* Target) {
			void* OldTarget = ReadJmpM64(proc, Instruction);

			uint32_t OldProtection = Process::details::RemoteProtect(proc._handle, Offset(Instruction, 6), sizeof(void*), PAGE_EXECUTE_READWRITE);
			Process::details::RemoteWrite<void*>(proc._handle, Offset(Instruction, 6), &Target);
			Process::details::RemoteProtect(proc._handle, Offset(Instruction, 6), sizeof(void*), OldProtection);
			return OldTarget;
		}
	}

	template<typename RetType, typename ...Args>
	SelfContained<RetType, Args...> CreateSCF(HANDLE Target, RetType(*Function)(Args...), const std::vector<void*>& kStackPointers) {
		std::vector<void*> StackPointers = {};
		StackPointers.reserve(kStackPointers.size() + 1);
		StackPointers.push_back(nullptr);

		for (void* Item : kStackPointers)
			StackPointers.push_back(Item);

		size_t FunctionSize = details::CalculateFunctionSize(Function);
		size_t StackSize = details::CalculateStackSize(StackPointers, FunctionSize);
		size_t PageSize = FunctionSize + StackSize;
		uintptr_t PageAddr = Process::details::RemoteAlloc<uintptr_t>(Target, PageSize, PAGE_READWRITE);
		FunctionData HData = {
			.Page = reinterpret_cast<void*>(PageAddr),
			.Size = PageSize
		};

		uintptr_t HDataAddr = Process::details::RemoteAlloc<uintptr_t>(Target, sizeof(FunctionData));
		Process::details::RemoteWrite(Target, HDataAddr, &HData, sizeof(FunctionData));
		StackPointers.front() = reinterpret_cast<void*>(HDataAddr);
		uintptr_t StackAddr = PageAddr + FunctionSize + sizeof(void*);
		StackAddr += (StackAddr % sizeof(void*));
		uintptr_t StackStart = StackAddr;
		uint8_t* FunctionBytes = new uint8_t[FunctionSize];
		std::memcpy(FunctionBytes, Function, FunctionSize);


		for (uintptr_t Offset = 0; Offset < FunctionSize; Offset++) {
			uint8_t* CurrentBytes = FunctionBytes + Offset;
			if (details::LocalRead<uintptr_t>(CurrentBytes) == ofssetss_new::SCF_MARKER_STK) {
				details::LocalWrite<uintptr_t>(CurrentBytes, StackAddr);
				Offset += sizeof(void*);
				continue;
			}
			if (details::LocalRead<uint32_t>(CurrentBytes) == ofssetss_new::SCF_END_MARKER) {
				details::LocalWrite<uint32_t>(CurrentBytes, 0x90909090); // NOP

			}
		}

		for (void* Item : StackPointers) {
			Process::details::RemoteWrite<void*>(Target, StackAddr, &Item);
			StackAddr += sizeof(void*);
		}

		Process::details::RemoteWrite(Target, PageAddr, FunctionBytes, FunctionSize);
		delete[] FunctionBytes;

		Process::details::RemoteProtect(Target, PageAddr, FunctionSize, PAGE_EXECUTE);

		SelfContained<RetType, Args...> Result = {};

		Result.Page = reinterpret_cast<void*>(PageAddr),
			Result.Size = PageSize;
		Result.HData = reinterpret_cast<void*>(HDataAddr);
		Result.Target = Target;

		return Result;
	}

	template<typename RetType, typename ...Args>
	void DestroySCF(SelfContained<RetType, Args...>& Data) {
		Process::details::RemoteFree(Data.Target, Data.Page, 0, MEM_RELEASE);
	}

	enum HOOK_STATUS {
		HOOK_IDLE,
		HOOK_RUNNING,
		HOOK_FINISHED,
		STATUS_1,
		STATUS_2,
		STATUS_3,
		STATUS_4,
		STATUS_5,
		STATUS_6,
		STATUS_7,
		STATUS_8,
		STATUS_9,
		STATUS_10,
		STATUS_11,
		STATUS_12,
		STATUS_13,
		STATUS_14,
		STATUS_15,
		STATUS_16,
		STATUS_17,
		STATUS_18,
		STATUS_19,
		STATUS_20,
		HOOK_FAILED = 21 // making HOOK_FAILED act like status 21
	};

	const char* STATUSES[] = {
		"HOOK_IDLE",
		"HOOK_RUNNING",
		"HOOK_FINISHED",
		"STATUS_1",
		"STATUS_2",
		"STATUS_3",
		"STATUS_4",
		"STATUS_5",
		"STATUS_6",
		"STATUS_7",
		"STATUS_8",
		"STATUS_9",
		"STATUS_10",
		"STATUS_11",
		"STATUS_12",
		"STATUS_13",
		"STATUS_14",
		"STATUS_15",
		"STATUS_16",
		"STATUS_17",
		"STATUS_18",
		"STATUS_19",
		"STATUS_20",
		"HOOK_FAILED"
	};

	template<typename RetType, typename ...Args>
	struct NtHook {
		void* Previous = nullptr;
		void* Status = nullptr;
		void* Stub = nullptr;
		Process::Object Target = {};
		SelfContained<RetType, Args...> Detour = {};
		NtHook() = default;
		NtHook(void* Previous, void* Status, void* Stub, SelfContained<RetType, Args...>& Detour) : Previous(Previous), Status(Status), Stub(Stub), Detour(Detour) {};
	};

	template<typename RetType, typename ...Args>
	NtHook<RetType, Args...> Hook(Process::Object& proc, const char* Name, RetType(*Detour)(Args...), const std::vector<void*>& ExtraStack) {
		Process::Module ntdll = proc.GetModule("ntdll.dll");

		void* Function = ntdll.GetAddress(Name);
		void* DynamicStub = Injector::details::ReadJmpRel32(proc, Function);
		if (!DynamicStub) {
			printf("[YUB-X] Failed to read JMP stub from %s\n", Name);
			return {};
		}

		void* Hook = Injector::details::ReadJmpM64(proc, DynamicStub);
		if (!Hook) {
			printf("[YUB-X] Failed to read JMP target from stub\n");
			return {};
		}

		void* Status = Process::details::RemoteAlloc(proc._handle, sizeof(uint32_t), PAGE_READWRITE);
		auto Val = Injector::HOOK_IDLE;
		Process::details::RemoteWrite(proc._handle, Status, &Val);

		std::vector<void*> Stack = { Hook, Status };
		Stack.insert(Stack.end(), ExtraStack.begin(), ExtraStack.end());

		auto SCF = Injector::CreateSCF(proc._handle, Detour, Stack);
		Injector::details::WriteJmpM64(proc, DynamicStub, SCF.Page);
		FlushInstructionCache(proc._handle, DynamicStub, 16);

		NtHook<RetType, Args...> Result = {};

		Result.Detour = SCF;
		Result.Previous = Hook;
		Result.Stub = DynamicStub;
		Result.Target = proc;
		Result.Status = Status;

		return Result;
	}

	template<typename RetType, typename ...Args>
	void Unhook(NtHook<RetType, Args...>& Data) {
		Injector::details::WriteJmpM64(Data.Target, Data.Stub, Data.Previous);
		FlushInstructionCache(Data.Target._handle, nullptr, 0);
		Process::details::RemoteFree(Data.Target._handle, Data.Status);
		Injector::DestroySCF(Data.Detour);
	}
}

namespace Types {
	using NtQuerySystemInformation = int32_t(__stdcall*)(uint32_t SystemInformationClass, void* SystemInformation, ULONG SystemInformationLength, ULONG* ReturnLength);

	namespace unordered_set {
		using insert = void* (__fastcall*)(void*, void*, void*);
	}
};

#define RELOC_FLAG(RelInfo) (((RelInfo) >> 12) == IMAGE_REL_BASED_DIR64)

#define CFG_PAGE_HASH_KEY  0xC9E94648;
#define CFG_VALIDATION_XOR 0xD;

#define HashPage(Page) \
    (((uintptr_t)(Page) >> 12) ^ CFG_PAGE_HASH_KEY)

#define ValidationByte(Page) \
    (((uintptr_t)(Page) >> 44) ^ CFG_VALIDATION_XOR)

#define BatchWhitelistRegion(Start, Size)                                                    \
{                                                                                            \
    uintptr_t __wl_start = (uintptr_t)(Start);                                               \
    uintptr_t __wl_size = (uintptr_t)(Size);                                                 \
    uintptr_t __wl_aligned_start = __wl_start & ~0xFFFULL;                                   \
    uintptr_t __wl_aligned_end = (__wl_start + __wl_size + 0xFFFULL) & ~0xFFFULL;            \
    for (uintptr_t __wl_page = __wl_aligned_start; __wl_page < __wl_aligned_end; __wl_page += 0x1000) \
    {                                                                                        \
        uint8_t __wl_stack_block[0x40] = {};                                                 \
        *reinterpret_cast<uint32_t*>(__wl_stack_block + 0x18) = HashPage(__wl_page);         \
        *reinterpret_cast<uint8_t*>(__wl_stack_block + 0x1C) = ValidationByte(__wl_page);    \
        insert_set(whitelist, __wl_stack_block + 0x14, __wl_stack_block + 0x18);             \
    }                                                                                        \
}

SCF_WRAP_START;
int32_t __stdcall NtQuerySystemInformation(
	uint32_t SystemInformationClass,
	void* SystemInformation,
	ULONG SystemInformationLength,
	ULONG* ReturnLength
) {
	SCF_START;

	FunctionData* DetourPage = reinterpret_cast<FunctionData*>(Stack[0]);
	auto Original = reinterpret_cast<Types::NtQuerySystemInformation>(Stack[1]);
	auto Status = reinterpret_cast<Injector::HOOK_STATUS*>(Stack[2]);
	auto insert_set = reinterpret_cast<Types::unordered_set::insert>(Stack[3]);

	void* whitelist = Stack[4];

	uintptr_t Base = reinterpret_cast<uintptr_t>(Stack[5]);
	auto _GetProcAddress = reinterpret_cast<decltype(&GetProcAddress)>(Stack[6]);
	auto _GetModuleHandleA = reinterpret_cast<decltype(&GetModuleHandleA)>(Stack[7]);
	auto _LoadLibraryA = reinterpret_cast<decltype(&LoadLibraryA)>(Stack[8]);

	uintptr_t bitmap = reinterpret_cast<uintptr_t>(Stack[10]);

	using RtlAddFunctionTable_t = BOOL(WINAPI*)(PRUNTIME_FUNCTION, DWORD, DWORD64);
	auto _RtlAddFunctionTable = reinterpret_cast<RtlAddFunctionTable_t>(Stack[11]);

	auto* Dos = reinterpret_cast<IMAGE_DOS_HEADER*>(Base);
	auto* Nt = reinterpret_cast<IMAGE_NT_HEADERS*>(Base + Dos->e_lfanew);
	auto* Opt = &Nt->OptionalHeader;
	auto Size = Opt->SizeOfImage;

	if (*Status == Injector::HOOK_IDLE) {
		*Status = Injector::HOOK_RUNNING;

		BatchWhitelistRegion(DetourPage->Page, DetourPage->Size);
		auto UDetourPage = (uintptr_t)(DetourPage->Page) & ~0xFFFF;
		for (auto pg = UDetourPage; pg < UDetourPage + DetourPage->Size; pg += 0x1000) {
			*reinterpret_cast<std::uint32_t*>(*reinterpret_cast<std::uintptr_t*>(bitmap) + (pg >> 0x13)) |=
				1 << ((pg >> 0x10 & 7) % 0x20);
		}

		BatchWhitelistRegion(Base, Size);
		Base &= ~0xFFFF;
		for (auto pg = Base; pg < Base + Size; pg += 0x1000) {
			*reinterpret_cast<std::uint32_t*>(*reinterpret_cast<std::uintptr_t*>(bitmap) + (pg >> 0x13)) |=
				1 << ((pg >> 0x10 & 7) % 0x20);
		}

		uintptr_t LocationDelta = Base - Opt->ImageBase;
		if (LocationDelta) {
			auto& RelocDir = Opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
			if (RelocDir.Size) {
				auto* pRelocData = reinterpret_cast<IMAGE_BASE_RELOCATION*>(Base + RelocDir.VirtualAddress);
				const auto* pRelocEnd = reinterpret_cast<IMAGE_BASE_RELOCATION*>(reinterpret_cast<uintptr_t>(pRelocData) + RelocDir.Size);

				while (pRelocData < pRelocEnd && pRelocData->SizeOfBlock) {
					UINT EntryCount = (pRelocData->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
					WORD* Relocs = reinterpret_cast<WORD*>(pRelocData + 1);

					for (UINT i = 0; i < EntryCount; ++i, ++Relocs) {
						if (RELOC_FLAG(*Relocs)) {
							UINT_PTR* Patch = reinterpret_cast<UINT_PTR*>(Base + pRelocData->VirtualAddress + ((*Relocs) & 0xFFF));
							*Patch += LocationDelta;
						}
					}
					pRelocData = reinterpret_cast<IMAGE_BASE_RELOCATION*>(reinterpret_cast<BYTE*>(pRelocData) + pRelocData->SizeOfBlock);
				}
			}
		}

		auto& sehDir = Opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
		if (sehDir.Size && sehDir.VirtualAddress) {
			_RtlAddFunctionTable(reinterpret_cast<PRUNTIME_FUNCTION>(Base + sehDir.VirtualAddress), sehDir.Size / sizeof(IMAGE_RUNTIME_FUNCTION_ENTRY), Base);
		}

		auto& ImportDir = Opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
		if (ImportDir.Size) {
			auto* ImportDesc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(Base + ImportDir.VirtualAddress);
			while (ImportDesc->Name) {
				char* ModName = reinterpret_cast<char*>(Base + ImportDesc->Name);
				HMODULE Mod = _GetModuleHandleA(ModName);
				if (!Mod) Mod = _LoadLibraryA(ModName);
				if (!Mod) { ++ImportDesc; continue; }

				auto* Dos = reinterpret_cast<IMAGE_DOS_HEADER*>(Mod);
				auto* Nt = reinterpret_cast<IMAGE_NT_HEADERS*>((uintptr_t)Mod + Dos->e_lfanew);
				auto SizeOfImage = Nt->OptionalHeader.SizeOfImage;
				BatchWhitelistRegion((uintptr_t)Mod, SizeOfImage);

				uintptr_t* Thunk = reinterpret_cast<uintptr_t*>(Base + ImportDesc->OriginalFirstThunk);
				uintptr_t* Func = reinterpret_cast<uintptr_t*>(Base + ImportDesc->FirstThunk);
				if (!Thunk) Thunk = Func;

				while (*Thunk) {
					if (IMAGE_SNAP_BY_ORDINAL(*Thunk)) {
						*Func = (uintptr_t)_GetProcAddress(Mod, reinterpret_cast<char*>(*Thunk & 0xFFFF));
					}
					else {
						auto* Import = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(Base + *Thunk);
						*Func = (uintptr_t)_GetProcAddress(Mod, Import->Name);
					}
					++Thunk; ++Func;
				}
				++ImportDesc;
			}
		}

		auto& TlsDir = Opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
		if (TlsDir.Size) {
			auto* Tls = reinterpret_cast<IMAGE_TLS_DIRECTORY*>(Base + TlsDir.VirtualAddress);
			if (Tls->AddressOfCallBacks) {
				auto* Callbacks = reinterpret_cast<PIMAGE_TLS_CALLBACK*>(Tls->AddressOfCallBacks);
				while (*Callbacks) {
					(*Callbacks)((LPVOID)Base, DLL_PROCESS_ATTACH, nullptr);
					++Callbacks;
				}
			}
		}

		__try {
			auto Entry = reinterpret_cast<int(__stdcall*)(HMODULE, DWORD, void*)>(Base + Opt->AddressOfEntryPoint);
			Entry(reinterpret_cast<HMODULE>(Base), DLL_PROCESS_ATTACH, nullptr);
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			*Status = Injector::HOOK_FINISHED;
			return Original(SystemInformationClass, SystemInformation, SystemInformationLength, ReturnLength);
		}

		*Status = Injector::HOOK_FINISHED;
	}

	return Original(SystemInformationClass, SystemInformation, SystemInformationLength, ReturnLength);
	SCF_END;
}
SCF_WRAP_END;

std::string current_timestamp() {
	using namespace std::chrono;
	auto now = system_clock::now();
	auto itt = system_clock::to_time_t(now);
	auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

	std::tm bt;
#ifdef _WIN32
	localtime_s(&bt, &itt);
#else
	localtime_r(&itt, &bt);
#endif

	std::ostringstream oss;
	oss << std::put_time(&bt, "[%Y-%m-%d %H:%M:%S") // time
		<< '.' << std::setfill('0') << std::setw(3) << ms.count() << "]";
	return oss.str();
} // simple timestamp


bool ManualMap(Process::Object& proc, std::string Path) { // mmap func
	Process::Module loader = proc.GetModule(oxorany("RobloxPlayerBeta.dll"));
	Process::Module kernelbase = proc.GetModule(oxorany("KERNELBASE.dll"));
	Process::Module user32 = proc.GetModule(oxorany("USER32.dll"));

#pragma region Write file into process
	std::cout << COLOR_INFO << current_timestamp() << " [!] reading dll: " << Path << COLOR_INFO << '\n'; // reads dll
	std::vector<uint8_t> Data = ReadFile(Path);
	if (Data.empty()) {
		return false;
	}

	uint8_t* Buffer = Data.data();

	IMAGE_DOS_HEADER* Dos = reinterpret_cast<IMAGE_DOS_HEADER*>(Buffer);
	IMAGE_NT_HEADERS* Nt = reinterpret_cast<IMAGE_NT_HEADERS*>(Buffer + Dos->e_lfanew);
	IMAGE_OPTIONAL_HEADER* OptHeader = &Nt->OptionalHeader;
	IMAGE_FILE_HEADER* FileHeader = &Nt->FileHeader;

	uintptr_t TargetBase = Process::details::RemoteAlloc<uintptr_t>(proc._handle, OptHeader->SizeOfImage, PAGE_EXECUTE_READWRITE);
	Process::details::RemoteWrite(proc._handle, TargetBase, Buffer, 0x1000);

	std::vector<IMAGE_SECTION_HEADER*> Sections = {};
	IMAGE_SECTION_HEADER* SectionHeader = IMAGE_FIRST_SECTION(Nt);
	for (uint32_t i = 0; i != FileHeader->NumberOfSections; ++i, ++SectionHeader) {
		if (SectionHeader->SizeOfRawData) {
			Sections.push_back(SectionHeader);
			Process::details::RemoteWrite(proc._handle, TargetBase + SectionHeader->VirtualAddress, Buffer + SectionHeader->PointerToRawData, SectionHeader->SizeOfRawData);
		}
	}
#pragma endregion


	void* _GetProcAddress = kernelbase.GetAddress(oxorany("GetProcAddress"));
	void* _GetModuleHandleA = kernelbase.GetAddress(oxorany("GetModuleHandleA"));
	void* _LoadLibraryA = kernelbase.GetAddress(oxorany("LoadLibraryA"));
	void* _MessageBoxA = user32.GetAddress(oxorany("MessageBoxA"));
	void* _RtlAddFunctionTable = GetProcAddress(GetModuleHandleA("kernel32.dll"), "RtlAddFunctionTable");

	auto NtHk = Injector::Hook(proc, "NtQuerySystemInformation", NtQuerySystemInformation, {
		(void*)(loader.Start + ofssetss_new::set_insert),
		(void*)(loader.Start + ofssetss_new::whitelist_page),
		(void*)TargetBase,
		_GetProcAddress,
		_GetModuleHandleA,
		_LoadLibraryA,
		_MessageBoxA,
		(void*)(loader.Start + ofssetss_new::Bitmap),
		_RtlAddFunctionTable
		});


	Injector::HOOK_STATUS Status = (Injector::HOOK_STATUS)-1;
	Injector::HOOK_STATUS PrevStatus = Status;
	bool Done = false;
	while (!Done) {
		Process::details::RemoteRead(proc._handle, NtHk.Status, &Status);
		if (Status != PrevStatus) {
			PrevStatus = Status;
		}
	switch (Status) {
		case Injector::HOOK_FINISHED:
			Done = true;
			std::cout << COLOR_OK << current_timestamp() << " [!] injection finished" << COLOR_INFO << '\n'; // finished mmap
			break;
		case Injector::HOOK_FAILED:
			std::cerr << COLOR_ERROR << current_timestamp() << " [!] injection failed" << COLOR_INFO << '\n'; // injection failed, fix ur shit
			Injector::Unhook(NtHk);
			return false;
		}
			std::this_thread::sleep_for(std::chrono::milliseconds(100)); // let shit breath
	}

	Injector::Unhook(NtHk);
	return true;
}

int main()
{
        std::cout << COLOR_INFO << current_timestamp() << " [+] waiting for roblox.." << COLOR_RESET << std::endl; 
	Process::Object proc = Process::WaitForProcess(oxorany(L"RobloxPlayerBeta.exe"));
	std::string dllname = oxorany("yubx.dll");
	while (true)
	{
		HWND hwnd = FindWindowW(nullptr, L"Roblox");
		if (hwnd != nullptr)
			break;
		Sleep(2000);
	}
	ManualMap(proc, dllname);
	return 0;
}

// happy skidding!11!!

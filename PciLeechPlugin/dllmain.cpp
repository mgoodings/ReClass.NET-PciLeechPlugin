#include "vmmdll.h"
#include <ReClassNET_Plugin.hpp>
#include <algorithm>
#include <cstdint>
#include <vector>

extern "C" void RC_CallConv EnumerateProcesses( EnumerateProcessCallback callbackProcess ) {
	if ( callbackProcess == nullptr ) {
		return;
	}

	// TODO: Ghetto init code, make this better
	static bool init = false;

	if ( !init ) {
		LPSTR argv[] = { "", "-device", "fpga" };
		BOOL result = VMMDLL_Initialize( 3, argv );

		if ( !result ) {
			MessageBoxA( 0, "FAIL: VMMDLL_Initialize", 0, MB_OK | MB_ICONERROR );

			ExitProcess( -1 );
		}

		init = true;
	}

	BOOL result;
	ULONG64 cPIDs = 0;
	DWORD i, *pPIDs = NULL;

	result =
	    VMMDLL_PidList( NULL, &cPIDs ) && ( pPIDs = ( DWORD* ) LocalAlloc( LMEM_ZEROINIT, cPIDs * sizeof( DWORD ) ) ) && VMMDLL_PidList( pPIDs, &cPIDs );

	if ( !result ) {
		LocalFree( pPIDs );
		return;
	}

	for ( i = 0; i < cPIDs; i++ ) {
		DWORD dwPID = pPIDs[ i ];

		VMMDLL_PROCESS_INFORMATION info;
		SIZE_T cbInfo = sizeof( VMMDLL_PROCESS_INFORMATION );
		ZeroMemory( &info, cbInfo );
		info.magic = VMMDLL_PROCESS_INFORMATION_MAGIC;
		info.wVersion = VMMDLL_PROCESS_INFORMATION_VERSION;

		result = VMMDLL_ProcessGetInformation( dwPID, &info, &cbInfo );

		if ( result ) {
			EnumerateProcessData data = {};
			data.Id = dwPID;
			MultiByteToUnicode( info.szNameLong, data.Name, PATH_MAXIMUM_LENGTH );

			LPSTR szPathUser = VMMDLL_ProcessGetInformationString( dwPID, VMMDLL_PROCESS_INFORMATION_OPT_STRING_PATH_USER_IMAGE );

			if ( szPathUser ) {
				MultiByteToUnicode( szPathUser, data.Path, PATH_MAXIMUM_LENGTH );
			}

			callbackProcess( &data );
		}
	}

	LocalFree( pPIDs );
}

extern "C" void RC_CallConv EnumerateRemoteSectionsAndModules( RC_Pointer handle, EnumerateRemoteSectionsCallback callbackSection,
							       EnumerateRemoteModulesCallback callbackModule ) {
	if ( callbackSection == nullptr && callbackModule == nullptr ) {
		return;
	}

	BOOL result;
	DWORD dwPID = ( DWORD ) handle;
	ULONG64 i, j;

	ULONG64 cMemMapEntries = 0;
	PVMMDLL_MEMMAP_ENTRY memMapEntry, pMemMapEntries = NULL;

	result = VMMDLL_ProcessGetMemoryMap( dwPID, NULL, &cMemMapEntries, TRUE ) && cMemMapEntries &&
		 ( pMemMapEntries = ( PVMMDLL_MEMMAP_ENTRY ) LocalAlloc( 0, cMemMapEntries * sizeof( VMMDLL_MEMMAP_ENTRY ) ) ) &&
		 VMMDLL_ProcessGetMemoryMap( dwPID, pMemMapEntries, &cMemMapEntries, TRUE );

	if ( !result ) {
		LocalFree( pMemMapEntries );

		return;
	}

	std::vector< EnumerateRemoteSectionData > sections;

	for ( i = 0; i < cMemMapEntries; i++ ) {
		memMapEntry = pMemMapEntries + i;

		EnumerateRemoteSectionData section = {};
		section.BaseAddress = ( RC_Pointer ) memMapEntry->AddrBase;
		section.Size = memMapEntry->cPages << 12;

		section.Protection = SectionProtection::NoAccess;
		section.Category = SectionCategory::Unknown;

		if ( memMapEntry->fPage & VMMDLL_MEMMAP_FLAG_PAGE_NS )
			section.Protection |= SectionProtection::Read;
		if ( memMapEntry->fPage & VMMDLL_MEMMAP_FLAG_PAGE_W )
			section.Protection |= SectionProtection::Write;
		if ( !( memMapEntry->fPage & VMMDLL_MEMMAP_FLAG_PAGE_NX ) )
			section.Protection |= SectionProtection::Execute;

		if ( memMapEntry->szTag[ 0 ] ) {
			if ( ( memMapEntry->szTag[ 0 ] == 'H' && memMapEntry->szTag[ 1 ] == 'E' && memMapEntry->szTag[ 2 ] == 'A' &&
			       memMapEntry->szTag[ 3 ] == 'P' ) ||
			     ( memMapEntry->szTag[ 0 ] == '[' && memMapEntry->szTag[ 1 ] == 'H' && memMapEntry->szTag[ 2 ] == 'E' &&
			       memMapEntry->szTag[ 3 ] == 'A' && memMapEntry->szTag[ 4 ] == 'P' ) ) {
				section.Type = SectionType::Private;

			} else {
				section.Type = SectionType::Image;

				MultiByteToUnicode( memMapEntry->szTag, section.ModulePath, PATH_MAXIMUM_LENGTH );
			}
		} else {
			section.Type = SectionType::Mapped;
		}

		sections.push_back( std::move( section ) );
	}

	LocalFree( pMemMapEntries );

	ULONG64 cModuleEntries = 0;
	PVMMDLL_MODULEMAP_ENTRY moduleEntry, pModuleEntries = NULL;

	result = VMMDLL_ProcessGetModuleMap( dwPID, NULL, &cModuleEntries ) && cModuleEntries &&
		 ( pModuleEntries = ( PVMMDLL_MODULEMAP_ENTRY ) LocalAlloc( 0, cModuleEntries * sizeof( VMMDLL_MODULEMAP_ENTRY ) ) ) &&
		 VMMDLL_ProcessGetModuleMap( dwPID, pModuleEntries, &cModuleEntries );

	if ( !result ) {
		LocalFree( pModuleEntries );

		return;
	}

	for ( i = 0; i < cModuleEntries; i++ ) {
		moduleEntry = pModuleEntries + i;

		EnumerateRemoteModuleData data = {};
		data.BaseAddress = ( RC_Pointer ) moduleEntry->BaseAddress;
		data.Size = ( RC_Size ) moduleEntry->SizeOfImage;
		MultiByteToUnicode( moduleEntry->szName, data.Path, PATH_MAXIMUM_LENGTH );

		callbackModule( &data );

		// !!!!!!!!!
		// <warning>
		// this code crashes some processes, possibly a bug with vmm.dll
		DWORD cSections = 0;
		PIMAGE_SECTION_HEADER sectionEntry, pSections = NULL;

		result = VMMDLL_ProcessGetSections( dwPID, moduleEntry->szName, NULL, 0, &cSections ) && cSections &&
			 ( pSections = ( PIMAGE_SECTION_HEADER ) LocalAlloc( 0, cSections * sizeof( IMAGE_SECTION_HEADER ) ) ) &&
			 VMMDLL_ProcessGetSections( dwPID, moduleEntry->szName, pSections, cSections, &cSections );

		if ( result ) {
			for ( j = 0; j < cSections; j++ ) {
				sectionEntry = pSections + j;

				auto it =
				    std::lower_bound( std::begin( sections ), std::end( sections ), reinterpret_cast< LPVOID >( moduleEntry->BaseAddress ),
						      [&sections ]( const auto& lhs, const LPVOID& rhs ) { return lhs.BaseAddress < rhs; } );

				auto sectionAddress = ( uintptr_t )( moduleEntry->BaseAddress + sectionEntry->VirtualAddress );

				for ( auto k = it; k != std::end( sections ); ++k ) {
					uintptr_t start = ( uintptr_t ) k->BaseAddress;
					uintptr_t end = ( uintptr_t ) k->BaseAddress + k->Size;

					if ( sectionAddress >= start && sectionAddress < end ) {
						// Copy the name because it is not null padded.
						char buffer[ IMAGE_SIZEOF_SHORT_NAME + 1 ] = { 0 };
						std::memcpy( buffer, sectionEntry->Name, IMAGE_SIZEOF_SHORT_NAME );

						if ( std::strcmp( buffer, ".text" ) == 0 || std::strcmp( buffer, "code" ) == 0 ) {
							k->Category = SectionCategory::CODE;
						} else if ( std::strcmp( buffer, ".data" ) == 0 || std::strcmp( buffer, "data" ) == 0 ||
							    std::strcmp( buffer, ".rdata" ) == 0 || std::strcmp( buffer, ".idata" ) == 0 ) {
							k->Category = SectionCategory::DATA;
						}

						MultiByteToUnicode( buffer, k->Name, IMAGE_SIZEOF_SHORT_NAME );
					}
				}
			}
		}

		LocalFree( pSections );
		// </warning>
		// !!!!!!!!!!
	}

	LocalFree( pModuleEntries );

	if ( callbackSection != nullptr ) {
		for ( auto&& section : sections ) {
			callbackSection( &section );
		}
	}
}

extern "C" RC_Pointer RC_CallConv OpenRemoteProcess( RC_Pointer id, ProcessAccess desiredAccess ) {
	return id;
}

extern "C" bool RC_CallConv IsProcessValid( RC_Pointer handle ) {
	VMMDLL_PROCESS_INFORMATION info;
	SIZE_T cbInfo = sizeof( VMMDLL_PROCESS_INFORMATION );
	ZeroMemory( &info, cbInfo );
	info.magic = VMMDLL_PROCESS_INFORMATION_MAGIC;
	info.wVersion = VMMDLL_PROCESS_INFORMATION_VERSION;

	if ( VMMDLL_ProcessGetInformation( ( DWORD ) handle, &info, &cbInfo ) ) {
		return true;
	}

	return false;
}

extern "C" void RC_CallConv CloseRemoteProcess( RC_Pointer handle ) {
}

extern "C" bool RC_CallConv ReadRemoteMemory( RC_Pointer handle, RC_Pointer address, RC_Pointer buffer, int offset, int size ) {
	buffer = reinterpret_cast< RC_Pointer >( reinterpret_cast< uintptr_t >( buffer ) + offset );

	if ( VMMDLL_MemRead( ( DWORD ) handle, ( ULONG64 ) address, ( PBYTE ) buffer, size ) ) {
		return true;
	}

	return false;
}

extern "C" bool RC_CallConv WriteRemoteMemory( RC_Pointer handle, RC_Pointer address, RC_Pointer buffer, int offset, int size ) {
	buffer = reinterpret_cast< RC_Pointer >( reinterpret_cast< uintptr_t >( buffer ) + offset );

	if ( VMMDLL_MemWrite( ( DWORD ) handle, ( ULONG64 ) address, ( PBYTE ) buffer, size ) ) {
		return true;
	}

	return false;
}

////////////////////////////////////////
////////////////////////////////////////
// Remote debugging is not supported
////////////////////////////////////////
////////////////////////////////////////

extern "C" void RC_CallConv ControlRemoteProcess( RC_Pointer handle, ControlRemoteProcessAction action ) {
}

extern "C" bool RC_CallConv AttachDebuggerToProcess( RC_Pointer id ) {
	return false;
}

extern "C" void RC_CallConv DetachDebuggerFromProcess( RC_Pointer id ) {
}

extern "C" bool RC_CallConv AwaitDebugEvent( DebugEvent* evt, int timeoutInMilliseconds ) {
	return false;
}

extern "C" void RC_CallConv HandleDebugEvent( DebugEvent* evt ) {
}

extern "C" bool RC_CallConv SetHardwareBreakpoint( RC_Pointer id, RC_Pointer address, HardwareBreakpointRegister reg, HardwareBreakpointTrigger type,
						   HardwareBreakpointSize size, bool set ) {
	return false;
}

## Notes on this fork
- Updated for MemProcFS v5.3

# ReClass.NET-PciLeechPlugin
A plugin that integrates vmm.dll from the https://github.com/ufrisk/MemProcFS project to allow ReClass.NET to function over a PCIe FPGA device.

## Usage

* Copy `PciLeechPlugin.dll` into the ReClass.NET\x64\Plugins directory
* Copy `leechcore.dll`, `vmm.dll` and `FTD3XX.dll` from MemProcFS into the ReClass.NET\x64 directory
* Open Reclass.NET, go to File -> Plugins
* Switch to the Native Helper tab and change the Functions Provider from Default to PciLeechPlugin

## Building

* Put this project in the same root directory as the ReClass.NET project
* Copy `leechcore.h`, `vmmdll.h` and `vmm.lib` from MemProcFS into the PciLeechPlugin directory

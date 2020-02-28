bootefi bin fdt
  install fdt or fdtcontroladdr
  (additionally, smbios)

grub
  devicetree fdt

  grub_fdt_load/install to install fdt
  from uefi configuration table or specified fdt

kernel/efi-stub
  set up fdt from uefi configuration
  jump into normal entry point

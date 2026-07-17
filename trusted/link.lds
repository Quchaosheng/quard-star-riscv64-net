OUTPUT_ARCH( "riscv" )

ENTRY( _start )

__stack_size = 0x4000;

MEMORY
{
	/* Fake ROM area */
	rom (rxa) : ORIGIN = 0xBF800000, LENGTH = 1M
	ram (wxa) : ORIGIN = 0xBF900000, LENGTH = 6M
}
PHDRS
{
	text PT_LOAD FLAGS(5);
	data PT_LOAD FLAGS(6);
}
SECTIONS
{
  .init :
	{
		_text = .;
		KEEP (*(SORT_NONE(.init)))
	} >rom AT>rom :text

  .text :
	{
		*(.text.unlikely .text.unlikely.*)
		*(.text.startup .text.startup.*)
		*(.text .text.*)
		*(.gnu.linkonce.t.*)
	} >rom AT>rom :text

  .fini :
	{
		KEEP (*(SORT_NONE(.fini)))
		_etext = .;
	} >rom AT>rom :text

  .rodata :
	{
		*(.rodata .rodata.*)
		*(.srodata .srodata.*)
	} >rom AT>rom :text
  /* 四字节对齐 */
	.rodata.align :
	{
		. = ALIGN(4);
		_rodata = .;
	} >rom AT>rom :text

  /*  标记 rodata的链接地址*/
	.rodata.start :
	{
		_rodata_lma = LOADADDR(.rodata.start);
	} >rom AT>rom :text

  .data.align :
	{
		. = ALIGN(4);
		_data = .;
	} >ram AT>rom :data

  .data.start :
	{
		_data_lma = LOADADDR(.data.start);
	} >ram AT>rom :data

  .data :
	{
		*(.data .data.*)
		*(.gnu.linkonce.d.*)
		. = ALIGN(8);
		PROVIDE( __global_pointer$ = . + 0x800 );
		*(.sdata .sdata.*)
		*(.sdata2 .sdata2.*)
		*(.gnu.linkonce.s.*)
		. = ALIGN(8);
		. = ALIGN(4);
		_edata = .;
	} >ram AT>rom :data

	.bss.align :
	{
		. = ALIGN(4);
		_bss = .;
	} >ram AT>rom :data

  .bss.start :
	{
		_bss_lma = LOADADDR(.bss.start);
	} >ram AT>rom :data

  .bss :
	{
		*(.sbss*)
		*(.gnu.linkonce.sb.*)
		*(.bss .bss.*)
		*(.gnu.linkonce.b.*)
		*(COMMON)

		. = ALIGN(4);
		_ebss = .;
	} >ram AT>rom :data


	. = ALIGN(8);
	_end = .;

  .stack :
	{
		. = ALIGN(16);
		. += __stack_size;
		_stack_top = .;
	} >ram AT>ram :data

}

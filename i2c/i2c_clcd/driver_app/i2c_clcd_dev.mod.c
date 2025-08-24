#include <linux/module.h>
#include <linux/export-internal.h>
#include <linux/compiler.h>

MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};



static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0xb1ad28e0, "__gnu_mcount_nc" },
	{ 0xc18b0895, "i2c_register_driver" },
	{ 0xefd6cf06, "__aeabi_unwind_cpp_pr0" },
	{ 0x2d222da1, "misc_deregister" },
	{ 0x950251f2, "i2c_del_driver" },
	{ 0x8e865d3c, "arm_delay_ops" },
	{ 0xe64cd205, "i2c_smbus_write_byte" },
	{ 0xae353d77, "arm_copy_from_user" },
	{ 0x828ce6bb, "mutex_lock" },
	{ 0x9618ede0, "mutex_unlock" },
	{ 0xf0fdf6cb, "__stack_chk_fail" },
	{ 0x8f678b07, "__stack_chk_guard" },
	{ 0x933d25b9, "devm_kmalloc" },
	{ 0xde4bf88b, "__mutex_init" },
	{ 0xf00a7918, "of_property_read_variable_u32_array" },
	{ 0x85c69635, "of_find_property" },
	{ 0xf9a482f9, "msleep" },
	{ 0xa2928db5, "misc_register" },
	{ 0x8ae804cf, "_dev_info" },
	{ 0x92997ed8, "_printk" },
	{ 0x46077962, "noop_llseek" },
	{ 0x8a3dbe73, "module_layout" },
};

MODULE_INFO(depends, "");

MODULE_ALIAS("i2c:pcf8574-hd44780");
MODULE_ALIAS("of:N*T*Cmy-i2c,pcf8574-hd44780");
MODULE_ALIAS("of:N*T*Cmy-i2c,pcf8574-hd44780C*");

MODULE_INFO(srcversion, "CBB4E0D3533CCF5B3BCAC1F");

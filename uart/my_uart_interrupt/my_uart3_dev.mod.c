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
	{ 0xde55e795, "_raw_spin_lock_irqsave" },
	{ 0xf3d0b495, "_raw_spin_unlock_irqrestore" },
	{ 0xefd6cf06, "__aeabi_unwind_cpp_pr0" },
	{ 0x2196324, "__aeabi_idiv" },
	{ 0xff178f6, "__aeabi_idivmod" },
	{ 0x92997ed8, "_printk" },
	{ 0x51a910c0, "arm_copy_to_user" },
	{ 0xf0fdf6cb, "__stack_chk_fail" },
	{ 0x8f678b07, "__stack_chk_guard" },
	{ 0x6e064dd8, "__register_chrdev" },
	{ 0x1d37eeed, "ioremap" },
	{ 0x6bc3fbc0, "__unregister_chrdev" },
	{ 0x92d5838e, "request_threaded_irq" },
	{ 0xedc03953, "iounmap" },
	{ 0xc1514a3b, "free_irq" },
	{ 0xae353d77, "arm_copy_from_user" },
	{ 0x5f754e5a, "memset" },
	{ 0x8fb6947b, "param_ops_bool" },
	{ 0xdcffdfcd, "param_ops_int" },
	{ 0x8a3dbe73, "module_layout" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "B339CAED39C357979A407C3");

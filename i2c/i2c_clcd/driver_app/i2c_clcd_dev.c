#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/of.h>
#include <linux/fs.h> 

#define CLCD_IOC_MAGIC 'L'
#define CLCD_IOC_CLEAR   _IO(CLCD_IOC_MAGIC, 0)
#define CLCD_IOC_HOME    _IO(CLCD_IOC_MAGIC, 1)
struct clcd_pos { __u8 row, col; };
#define CLCD_IOC_SETPOS  _IOW(CLCD_IOC_MAGIC, 2, struct clcd_pos)

struct clcd {
    struct i2c_client *client;
    struct miscdevice miscdev;
    struct mutex lock;
    u8 rows, cols;
    u8 pin_rs, pin_rw, pin_en;
    u8 pin_d4, pin_d5, pin_d6, pin_d7;
    u8 pin_bl;
    bool bl_active_high, bl_on;
};

static inline int pcf8574_write(struct clcd *l, u8 v)
{
    u8 m = 1 << l->pin_bl;
    v = l->bl_on == l->bl_active_high ? (v | m) : (v & ~m);
    return i2c_smbus_write_byte(l->client, v);
}
static inline void pulse_en(struct clcd *l, u8 v)
{
    pcf8574_write(l, v | (1 << l->pin_en));
    udelay(1);
    pcf8574_write(l, v & ~(1 << l->pin_en));
    udelay(50);
}
static void write4(struct clcd *l, u8 n, bool rs)
{
    u8 v = rs ? (1 << l->pin_rs) : 0;
    if (n & 1) v |= 1 << l->pin_d4;
    if (n & 2) v |= 1 << l->pin_d5;
    if (n & 4) v |= 1 << l->pin_d6;
    if (n & 8) v |= 1 << l->pin_d7;
    pulse_en(l, v);
}
static void send8(struct clcd *l, u8 b, bool rs)
{
    write4(l, b >> 4, rs);
    write4(l, b & 0x0F, rs);
}
static inline void lcd_cmd(struct clcd *l, u8 c)  { send8(l, c, false); }
static inline void lcd_data(struct clcd *l, u8 d) { send8(l, d, true);  }

static void lcd_setpos(struct clcd *l, u8 row, u8 col)
{
    static const u8 base[4] = {0x00, 0x40, 0x14, 0x54};
    if (row >= l->rows) row = 0;
    if (col >= l->cols) col = 0;
    lcd_cmd(l, 0x80 | (base[row] + col));
}
static void lcd_init(struct clcd *l)
{
    msleep(50);
    write4(l, 0x3, 0); msleep(5);
    write4(l, 0x3, 0); udelay(150);
    write4(l, 0x3, 0); udelay(150);
    write4(l, 0x2, 0);
    lcd_cmd(l, 0x28);  
    lcd_cmd(l, 0x0C);  
    lcd_cmd(l, 0x06);  
    lcd_cmd(l, 0x01); msleep(2);
}

static ssize_t clcd_write(struct file *f, const char __user *ubuf,
              size_t len, loff_t *ppos)
{
    struct miscdevice *m = f->private_data;
    struct clcd *l = container_of(m, struct clcd, miscdev);
    char k[128]; size_t n = min(len, sizeof(k));
    int i, row = 0, col = 0;

    if (!n) return 0;
    if (copy_from_user(k, ubuf, n)) return -EFAULT;

    mutex_lock(&l->lock);
    for (i = 0; i < n; i++) {
        char c = k[i];
        if (c == '\n') { row++; col = 0; lcd_setpos(l, row, 0); continue; }
        lcd_data(l, c);
        if (++col >= l->cols) { row++; col = 0; lcd_setpos(l, row, 0); }
        udelay(40);
    }
    mutex_unlock(&l->lock);
    return n;
}
static long clcd_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
    struct miscdevice *m = f->private_data;
    struct clcd *l = container_of(m, struct clcd, miscdev);

    switch (cmd) {
    case CLCD_IOC_CLEAR:
        mutex_lock(&l->lock); lcd_cmd(l, 0x01); msleep(2); lcd_setpos(l,0,0); mutex_unlock(&l->lock); return 0;
    case CLCD_IOC_HOME:
        mutex_lock(&l->lock); lcd_cmd(l, 0x02); msleep(2); mutex_unlock(&l->lock); return 0;
    case CLCD_IOC_SETPOS: {
        struct clcd_pos p;
        if (copy_from_user(&p, (void __user *)arg, sizeof(p))) return -EFAULT;
        mutex_lock(&l->lock); lcd_setpos(l, p.row, p.col); mutex_unlock(&l->lock); return 0;
    }
    default: return -ENOTTY;
    }
}
static const struct file_operations clcd_fops = {
    .owner = THIS_MODULE,
    .write = clcd_write,
    .unlocked_ioctl = clcd_ioctl,
    .llseek = noop_llseek,
};


static int clcd_probe(struct i2c_client *client)
{
    struct clcd *l = devm_kzalloc(&client->dev, sizeof(*l), GFP_KERNEL);
    int ret;
    u32 tmp;
    if (!l) return -ENOMEM;

    l->client = client; mutex_init(&l->lock);
    l->rows=2; l->cols=16;
    l->pin_rs=0; l->pin_rw=1; l->pin_en=2;
    l->pin_bl=3; l->pin_d4=4; l->pin_d5=5; l->pin_d6=6; l->pin_d7=7;
    l->bl_active_high=true; l->bl_on=true;

    if (client->dev.of_node) {
        if (!of_property_read_u32(client->dev.of_node, "rows", &tmp))
            l->rows = tmp;
        if (!of_property_read_u32(client->dev.of_node, "cols", &tmp))
            l->cols = tmp;
        if (!of_property_read_u32(client->dev.of_node, "rs-bit", &tmp))
            l->pin_rs = tmp;
        if (!of_property_read_u32(client->dev.of_node, "rw-bit", &tmp))
            l->pin_rw = tmp;
        if (!of_property_read_u32(client->dev.of_node, "en-bit", &tmp))
            l->pin_en = tmp;
        if (!of_property_read_u32(client->dev.of_node, "d4-bit", &tmp))
            l->pin_d4 = tmp;
        if (!of_property_read_u32(client->dev.of_node, "d5-bit", &tmp))
            l->pin_d5 = tmp;
        if (!of_property_read_u32(client->dev.of_node, "d6-bit", &tmp))
            l->pin_d6 = tmp;
        if (!of_property_read_u32(client->dev.of_node, "d7-bit", &tmp))
            l->pin_d7 = tmp;
        if (!of_property_read_u32(client->dev.of_node, "bl-bit", &tmp))
            l->pin_bl = tmp;
        l->bl_active_high = of_property_read_bool(client->dev.of_node, "bl-active-high");
    }

    lcd_init(l);

    l->miscdev.minor = MISC_DYNAMIC_MINOR;
    l->miscdev.name  = "clcd0";
    l->miscdev.fops  = &clcd_fops;

    ret = misc_register(&l->miscdev);
    if (ret) return ret;

    i2c_set_clientdata(client, l);
    dev_info(&client->dev, "clcd ready %ux%u addr=0x%02x\n", l->cols, l->rows, client->addr);

    pr_info("CLCD: rows=%d cols=%d rs=%d rw=%d en=%d d4=%d d5=%d d6=%d d7=%d bl=%d bl_active_high=%d\n",
        l->rows, l->cols, l->pin_rs, l->pin_rw, l->pin_en, l->pin_d4, l->pin_d5, l->pin_d6, l->pin_d7, l->pin_bl, l->bl_active_high);

    return 0;
}

static void clcd_remove(struct i2c_client *client)
{
    struct clcd *l = i2c_get_clientdata(client);
    misc_deregister(&l->miscdev);
}

static const struct of_device_id clcd_of_match[] = {
    { .compatible = "my-i2c,pcf8574-hd44780" }, { }
};
MODULE_DEVICE_TABLE(of, clcd_of_match);

static const struct i2c_device_id clcd_id[] = {
    { "pcf8574-hd44780", 0 }, { }
};
MODULE_DEVICE_TABLE(i2c, clcd_id);

static struct i2c_driver clcd_drv = {
    .driver = {
        .name = "pcf8574-clcd",
        .of_match_table = clcd_of_match,
    },
    .probe    = clcd_probe,
    .remove   = clcd_remove,
    .id_table = clcd_id,
};
module_i2c_driver(clcd_drv);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jeong")
MODULE_DESCRIPTION("HD44780 over PCF8574");
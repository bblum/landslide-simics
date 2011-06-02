magic_opcodes = {
    # cs410_core_haps
    "in_simics"           : 0x04100000L,
    "memsize"             : 0x04100001L,
    "lputs"               : 0x04100002L,
    "breakpoint"          : 0x04100003L,

    # cs410_dispatch
    "halt"                : 0x04100004L,

    # 410mods-dynamic-userdebug
    "reg_process"         : 0x04100005L,
    "unreg_process"       : 0x04100006L,
    "reg_child"           : 0x04100007L,

    # cs410_boot_assist
    "booted"              : 0x04100008L,

    # 410mods-dynamic-tidinfo
    "tidinfo_set"         : 0x04100009L,
    "tidinfo_del"         : 0x0410000AL,

    # More-or-less internal stuff below this line.

    # 410mods-dynamic-cp1
    "ck1"                 : 0x04108000L,

    # 410mods-dynamic-xchg
    "swat"                : 0x04108001L,

    # cs410_osdev
    "osdev_init"          : 0x04108002L,
    "osdev_next"          : 0x04108003L,

    # Fritz
    "fr_inkeys"           : 0x04108004L,
    "fr_inbuf"            : 0x04108005L,
    "fr_prog"             : 0x04108006L,
    "fr_here"             : 0x04108007L,

    # 410mods-dynamic-timer
    "schedint"            : 0x04108008L,

    # 410mods-dynamic-interleave
    "inter_learn"         : 0x04108009L,
    "inter_switch"        : 0x0410800AL,
}

package lz.android.shell.processins;

public enum Opcode {
    NOP(0, "nop", 1),
    MOVE(1, "move", 1),
    MOVE_FROM16(2, "move/from16", 2),
    MOVE_16(3, "move/16", 3),
    MOVE_WIDE(4, "move-wide", 1),
    MOVE_WIDE_FROM16(5, "move-wide/from16", 2),
    MOVE_WIDE_16(6, "move-wide/16", 3),
    MOVE_OBJECT(7, "move-object", 1),
    MOVE_OBJECT_FROM16(8, "move-object/from16", 2),
    MOVE_OBJECT_16(9, "move-object/16", 3),
    MOVE_RESULT(10, "move-result", 1),
    MOVE_RESULT_WIDE(11, "move-result-wide", 1),
    MOVE_RESULT_OBJECT(12, "move-result-object", 1),
    MOVE_EXCEPTION(13, "move-exception", 1),
    RETURN_VOID(14, "return-void", 1),
    RETURN(15, "return", 1),
    RETURN_WIDE(16, "return-wide", 1),
    RETURN_OBJECT(17, "return-object", 1),
    CONST_4(18, "const/4", 1),
    CONST_16(19, "const/16", 2),
    CONST(20, "const", 3),
    CONST_HIGH16(21, "const/high16", 2),
    CONST_WIDE_16(22, "const-wide/16", 2),
    CONST_WIDE_32(23, "const-wide/32", 3),
    CONST_WIDE(24, "const-wide", 5),
    CONST_WIDE_HIGH16(25, "const-wide/high16", 2),
    CONST_STRING(26, "const-string", 2),
    CONST_STRING_JUMBO(27, "const-string/jumbo", 3),
    CONST_CLASS(28, "const-class", 2),
    MONITOR_ENTER(29, "monitor-enter", 1),
    MONITOR_EXIT(30, "monitor-exit", 1),
    CHECK_CAST(31, "check-cast", 2),
    INSTANCE_OF(32, "instance-of", 2),
    ARRAY_LENGTH(33, "array-length", 1),
    NEW_INSTANCE(34, "new-instance", 2),
    NEW_ARRAY(35, "new-array", 2),
    FILLED_NEW_ARRAY(36, "filled-new-array", 3),
    FILLED_NEW_ARRAY_RANGE(37, "filled-new-array/range", 3),
    FILL_ARRAY_DATA(38, "fill-array-data", 3),
    THROW(39, "throw", 1),
    GOTO(40, "goto", 1),
    GOTO_16(41, "goto/16", 2),
    GOTO_32(42, "goto/32", 3),
    PACKED_SWITCH(43, "packed-switch", 3),
    SPARSE_SWITCH(44, "sparse-switch", 3),
    CMPL_FLOAT(45, "cmpl-float", 2),
    CMPG_FLOAT(46, "cmpg-float", 2),
    CMPL_DOUBLE(47, "cmpl-double", 2),
    CMPG_DOUBLE(48, "cmpg-double", 2),
    CMP_LONG(49, "cmp-long", 2),
    IF_EQ(50, "if-eq", 2),
    IF_NE(51, "if-ne", 2),
    IF_LT(52, "if-lt", 2),
    IF_GE(53, "if-ge", 2),
    IF_GT(54, "if-gt", 2),
    IF_LE(55, "if-le", 2),
    IF_EQZ(56, "if-eqz", 2),
    IF_NEZ(57, "if-nez", 2),
    IF_LTZ(58, "if-ltz", 2),
    IF_GEZ(59, "if-gez", 2),
    IF_GTZ(60, "if-gtz", 2),
    IF_LEZ(61, "if-lez", 2),
    OP_UNUSED_3E(62, "unused_3e", 1),
    OP_UNUSED_3F(63, "unused_3e", 1),
    OP_UNUSED_40(64, "unused_3e", 1),
    OP_UNUSED_41(65, "unused_3e", 1),
    OP_UNUSED_42(66, "unused_3e", 1),
    OP_UNUSED_43(67, "unused_3e", 1),
    AGET(68, "aget", 2),
    AGET_WIDE(69, "aget-wide", 2),
    AGET_OBJECT(70, "aget-object", 2),
    AGET_BOOLEAN(71, "aget-boolean", 2),
    AGET_BYTE(72, "aget-byte", 2),
    AGET_CHAR(73, "aget-char", 2),
    AGET_SHORT(74, "aget-short", 2),
    APUT(75, "aput", 2),
    APUT_WIDE(76, "aput-wide", 2),
    APUT_OBJECT(77, "aput-object", 2),
    APUT_BOOLEAN(78, "aput-boolean", 2),
    APUT_BYTE(79, "aput-byte", 2),
    APUT_CHAR(80, "aput-char", 2),
    APUT_SHORT(81, "aput-short", 2),
    IGET(82, "iget", 2),
    IGET_WIDE(83, "iget-wide", 2),
    IGET_OBJECT(84, "iget-object", 2),
    IGET_BOOLEAN(85, "iget-boolean", 2),
    IGET_BYTE(86, "iget-byte", 2),
    IGET_CHAR(87, "iget-char", 2),
    IGET_SHORT(88, "iget-short", 2),
    IPUT(89, "iput", 2),
    IPUT_WIDE(90, "iput-wide", 2),
    IPUT_OBJECT(91, "iput-object", 2),
    IPUT_BOOLEAN(92, "iput-boolean", 2),
    IPUT_BYTE(93, "iput-byte", 2),
    IPUT_CHAR(94, "iput-char", 2),
    IPUT_SHORT(95, "iput-short", 2),
    SGET(96, "sget", 2),
    SGET_WIDE(97, "sget-wide", 2),
    SGET_OBJECT(98, "sget-object", 2),
    SGET_BOOLEAN(99, "sget-boolean", 2),
    SGET_BYTE(100, "sget-byte", 2),
    SGET_CHAR(101, "sget-char", 2),
    SGET_SHORT(102, "sget-short", 2),
    SPUT(103, "sput", 2),
    SPUT_WIDE(104, "sput-wide", 2),
    SPUT_OBJECT(105, "sput-object", 2),
    SPUT_BOOLEAN(106, "sput-boolean", 2),
    SPUT_BYTE(107, "sput-byte", 2),
    SPUT_CHAR(108, "sput-char", 2),
    SPUT_SHORT(109, "sput-short", 2),
    INVOKE_VIRTUAL(110, "invoke-virtual", 3),
    INVOKE_SUPER(111, "invoke-super", 3),
    INVOKE_DIRECT(112, "invoke-direct", 3),
    INVOKE_STATIC(113, "invoke-static", 3),
    INVOKE_INTERFACE(114, "invoke-interface", 3),
    OP_UNUSED_73(115, "unused", 1),
    INVOKE_VIRTUAL_RANGE(116, "invoke-virtual/range", 3),
    INVOKE_SUPER_RANGE(117, "invoke-super/range", 3),
    INVOKE_DIRECT_RANGE(118, "invoke-direct/range", 3),
    INVOKE_STATIC_RANGE(119, "invoke-static/range", 3),
    INVOKE_INTERFACE_RANGE(120, "invoke-interface/range", 3),
    OP_UNUSED_79(121, "unused", 1),
    OP_UNUSED_7A(122, "unuesd", 1),
    NEG_INT(123, "neg-int", 1),
    NOT_INT(124, "not-int", 1),
    NEG_LONG(125, "neg-long", 1),
    NOT_LONG(126, "not-long", 1),
    NEG_FLOAT(127, "neg-float", 1),
    NEG_DOUBLE(128, "neg-double", 1),
    INT_TO_LONG(129, "int-to-long", 1),
    INT_TO_FLOAT(130, "int-to-float", 1),
    INT_TO_DOUBLE(131, "int-to-double", 1),
    LONG_TO_INT(132, "long-to-int", 1),
    LONG_TO_FLOAT(133, "long-to-float", 1),
    LONG_TO_DOUBLE(134, "long-to-double", 1),
    FLOAT_TO_INT(135, "float-to-int", 1),
    FLOAT_TO_LONG(136, "float-to-long", 1),
    FLOAT_TO_DOUBLE(137, "float-to-double", 1),
    DOUBLE_TO_INT(138, "double-to-int", 1),
    DOUBLE_TO_LONG(139, "double-to-long", 1),
    DOUBLE_TO_FLOAT(140, "double-to-float", 1),
    INT_TO_BYTE(141, "int-to-byte", 1),
    INT_TO_CHAR(142, "int-to-char", 1),
    INT_TO_SHORT(143, "int-to-short", 1),
    ADD_INT(144, "add-int", 2),
    SUB_INT(145, "sub-int", 2),
    MUL_INT(146, "mul-int", 2),
    DIV_INT(147, "div-int", 2),
    REM_INT(148, "rem-int", 2),
    AND_INT(149, "and-int", 2),
    OR_INT(150, "or-int", 2),
    XOR_INT(151, "xor-int", 2),
    SHL_INT(152, "shl-int", 2),
    SHR_INT(153, "shr-int", 2),
    USHR_INT(154, "ushr-int", 2),
    ADD_LONG(155, "add-long", 2),
    SUB_LONG(156, "sub-long", 2),
    MUL_LONG(157, "mul-long", 2),
    DIV_LONG(158, "div-long", 2),
    REM_LONG(159, "rem-long", 2),
    AND_LONG(160, "and-long", 2),
    OR_LONG(161, "or-long", 2),
    XOR_LONG(162, "xor-long", 2),
    SHL_LONG(163, "shl-long", 2),
    SHR_LONG(164, "shr-long", 2),
    USHR_LONG(165, "ushr-long", 2),
    ADD_FLOAT(166, "add-float", 2),
    SUB_FLOAT(167, "sub-float", 2),
    MUL_FLOAT(168, "mul-float", 2),
    DIV_FLOAT(169, "div-float", 2),
    REM_FLOAT(170, "rem-float", 2),
    ADD_DOUBLE(171, "add-double", 2),
    SUB_DOUBLE(172, "sub-double", 2),
    MUL_DOUBLE(173, "mul-double", 2),
    DIV_DOUBLE(174, "div-double", 2),
    REM_DOUBLE(175, "rem-double", 2),
    ADD_INT_2ADDR(176, "add-int/2addr", 1),
    SUB_INT_2ADDR(177, "sub-int/2addr", 1),
    MUL_INT_2ADDR(178, "mul-int/2addr", 1),
    DIV_INT_2ADDR(179, "div-int/2addr", 1),
    REM_INT_2ADDR(180, "rem-int/2addr", 1),
    AND_INT_2ADDR(181, "and-int/2addr", 1),
    OR_INT_2ADDR(182, "or-int/2addr", 1),
    XOR_INT_2ADDR(183, "xor-int/2addr", 1),
    SHL_INT_2ADDR(184, "shl-int/2addr", 1),
    SHR_INT_2ADDR(185, "shr-int/2addr", 1),
    USHR_INT_2ADDR(186, "ushr-int/2addr", 1),
    ADD_LONG_2ADDR(187, "add-long/2addr", 1),
    SUB_LONG_2ADDR(188, "sub-long/2addr", 1),
    MUL_LONG_2ADDR(189, "mul-long/2addr", 1),
    DIV_LONG_2ADDR(190, "div-long/2addr", 1),
    REM_LONG_2ADDR(191, "rem-long/2addr", 1),
    AND_LONG_2ADDR(192, "and-long/2addr", 1),
    OR_LONG_2ADDR(193, "or-long/2addr", 1),
    XOR_LONG_2ADDR(194, "xor-long/2addr", 1),
    SHL_LONG_2ADDR(195, "shl-long/2addr", 1),
    SHR_LONG_2ADDR(196, "shr-long/2addr", 1),
    USHR_LONG_2ADDR(197, "ushr-long/2addr", 1),
    ADD_FLOAT_2ADDR(198, "add-float/2addr", 1),
    SUB_FLOAT_2ADDR(199, "sub-float/2addr", 1),
    MUL_FLOAT_2ADDR(200, "mul-float/2addr", 1),
    DIV_FLOAT_2ADDR(201, "div-float/2addr", 1),
    REM_FLOAT_2ADDR(202, "rem-float/2addr", 1),
    ADD_DOUBLE_2ADDR(203, "add-double/2addr", 1),
    SUB_DOUBLE_2ADDR(204, "sub-double/2addr", 1),
    MUL_DOUBLE_2ADDR(205, "mul-double/2addr", 1),
    DIV_DOUBLE_2ADDR(206, "div-double/2addr", 1),
    REM_DOUBLE_2ADDR(207, "rem-double/2addr", 1),
    ADD_INT_LIT16(208, "add-int/lit16", 2),
    RSUB_INT(209, "rsub-int", 2),
    MUL_INT_LIT16(210, "mul-int/lit16", 2),
    DIV_INT_LIT16(211, "div-int/lit16", 2),
    REM_INT_LIT16(212, "rem-int/lit16", 2),
    AND_INT_LIT16(213, "and-int/lit16", 2),
    OR_INT_LIT16(214, "or-int/lit16", 2),
    XOR_INT_LIT16(215, "xor-int/lit16", 2),
    ADD_INT_LIT8(216, "add-int/lit8", 2),
    RSUB_INT_LIT8(217, "rsub-int/lit8", 2),
    MUL_INT_LIT8(218, "mul-int/lit8", 2),
    DIV_INT_LIT8(219, "div-int/lit8", 2),
    REM_INT_LIT8(220, "rem-int/lit8", 2),
    AND_INT_LIT8(221, "and-int/lit8", 2),
    OR_INT_LIT8(222, "or-int/lit8", 2),
    XOR_INT_LIT8(223, "xor-int/lit8", 2),
    SHL_INT_LIT8(224, "shl-int/lit8", 2),
    SHR_INT_LIT8(225, "shr-int/lit8", 2),
    USHR_INT_LIT8(226, "ushr-int/lit8", 2),
    IGET_VOLATILE(227, "iget-volatile", 2),
    IPUT_VOLATILE(228, "iput-volatile", 2),
    SGET_VOLATILE(229, "sget-volatile", 2),
    SPUT_VOLATILE(230, "sput-volatile", 2),
    IGET_OBJECT_VOLATILE(231, "iget-object-volatile", 2),
    IGET_WIDE_VOLATILE(232, "iget-wide-volatile", 2),
    IPUT_WIDE_VOLATILE(233, "iput-wide-volatile", 2),
    SGET_WIDE_VOLATILE(234, "sget-wide-volatile", 2),
    SPUT_WIDE_VOLATILE(235, "sput-wide-volatile", 2),
    OP_BREAKPOINT(236, "breakpoint", 1),
    THROW_VERIFICATION_ERROR(237, "throw-verification-error", 2),
    EXECUTE_INLINE(238, "execute-inline", 3),
    EXECUTE_INLINE_RANGE(239, "execute-inline/range", 3),
    INVOKE_OBJECT_INIT_RANGE(240, "invoke-object-init/range", 3),
    RETURN_VOID_BARRIER(241, "return-void-barrier", 1),
    IGET_QUICK(242, "iget-quick", 2),
    IGET_WIDE_QUICK(243, "iget-wide-quick", 2),
    IGET_OBJECT_QUICK(244, "iget-object-quick", 2),
    IPUT_QUICK(245, "iput-quick", 2),
    IPUT_WIDE_QUICK(246, "iput-wide-quick", 2),
    IPUT_OBJECT_QUICK(247, "iput-object-quick", 2),
    INVOKE_VIRTUAL_QUICK(248, "invoke-virtual-quick", 3),
    INVOKE_VIRTUAL_QUICK_RANGE(249, "invoke-virtual-quick/range", 3),
    INVOKE_SUPER_QUICK(250, "invoke-super-quick", 3),
    INVOKE_SUPER_QUICK_RANGE(251, "invoke-super-quick/range", 3),
    IPUT_OBJECT_VOLATILE(252, "iput-object-volatile", 2),
    SGET_OBJECT_VOLATILE(253, "sget-object-volatile", 2),
    SPUT_OBJECT_VOLATILE(254, "sput-object-volatile", 2),
    OP_UNUSED_FF(255, "unknown opcode", 1);

    public short value;
    public final String name;
    public final short jumboOpcodeValue;

    Opcode(int opcodeValue, String opcodeName, int jumboOpcodeValue) {
        this.value = (short) opcodeValue;
        this.name = opcodeName;
        this.jumboOpcodeValue = (short) jumboOpcodeValue;
    }

}

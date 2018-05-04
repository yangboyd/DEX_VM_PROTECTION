#include "stdafx.h"
#include "DexOpcodes.h"
#include "Exception.h"
#include "InterpC.h"
#include <assert.h>
#include "InterpDir/Interp.h"
#include <vector>
#include <math.h>

using namespace std;
//////////////////////////////////////////////////////////////////////////

inline void dvmAbort(void) {
    exit(1);
}

//////////////////////////////////////////////////////////////////////////

static const char kSpacing[] = "            ";

//////////////////////////////////////////////////////////////////////////



/**
 * ��ò����Ĵ���������
 * @param[in] separatorData Separator���ݡ�
 * @return ���ز����Ĵ���������
 */
static size_t getParamRegCount(const SeparatorData *separatorData) {
    int count = 0;

    for (int i = 0; i < separatorData->paramShortDesc.size; i++) {
        switch (separatorData->paramShortDesc.str[i]) {
            case 'Z':
            case 'B':
            case 'S':
            case 'C':
            case 'I':
            case 'F':
            case 'L':
            case '[':
                count++;
                break;
            case 'J'://long
            case 'D':
                count += 2;
                break;
            default:
                MY_LOG_ERROR("��Ч��������");
                break;
        }
    }
    return count;
}


/**
 * �Ƿ��Ǿ�̬������
 * @param[in] separatorData Separator���ݡ�
 * @return true���Ǿ�̬������false�����Ǿ�̬������
 */
static inline bool isStaticMethod(const SeparatorData *separatorData) {
    jboolean res = separatorData->accessFlag & ACC_STATIC;
    if (res == 0)
        return false;
    else
        return true;
    //return separatorData->accessFlag & ACC_STATIC == 0 ? false : true;

}


/**
 * �����ɱ��������ò������顣
 * @param[in]
 * @param[in]
 * @return ���ز������顣�������ʹ�������Ҫ�ͷ��ڴ档
 */
static jvalue *getParams(const SeparatorData *separatorData, va_list args) {
    jvalue *params = new jvalue[separatorData->paramSize];
    for (int i = 0; i < separatorData->paramSize; i++) {
        switch (separatorData->paramShortDesc.str[i]) {
            case 'Z':
                params[i].z = va_arg(args, jboolean);
                break;

            case 'B':
                params[i].b = va_arg(args, jbyte);
                break;

            case 'S':
                params[i].s = va_arg(args, jshort);
                break;

            case 'C':
                params[i].c = va_arg(args, jchar);
                break;

            case 'I':
                params[i].i = va_arg(args, jint);
                break;

            case 'J':
                params[i].j = va_arg(args, jlong);
                break;

            case 'F':
                params[i].f = va_arg(args, jfloat);
                break;

            case 'D':
                params[i].d = va_arg(args, jdouble);
                break;

            case 'L':
                params[i].l = va_arg(args, jobject);
                break;

            case '[':
                params[i].l = va_arg(args, jarray);
                break;
            default:
                MY_LOG_WARNING("???????????????????");
                break;
        }
    }
    return params;
}

//////////////////////////////////////////////////////////////////////////

/* get a long from an array of u4 */
static inline s8 getLongFromArray(const u4 *ptr, int idx) {
#if defined(NO_UNALIGN_64__UNION)
    union { s8 ll; u4 parts[2]; } conv;

    ptr += idx;
    conv.parts[0] = ptr[0];
    conv.parts[1] = ptr[1];
    return conv.ll;
#else
    s8 val;
    memcpy(&val, &ptr[idx], 8);
    return val;
#endif
}

/* store a long into an array of u4 */
static inline void putLongToArray(u4 *ptr, int idx, s8 val) {
#if defined(NO_UNALIGN_64__UNION)
    union { s8 ll; u4 parts[2]; } conv;

    ptr += idx;
    conv.ll = val;
    ptr[0] = conv.parts[0];
    ptr[1] = conv.parts[1];
#else
    memcpy(&ptr[idx], &val, 8);
#endif
}

/* get a double from an array of u4 */
static inline double getDoubleFromArray(const u4 *ptr, int idx) {
#if defined(NO_UNALIGN_64__UNION)
    union { double d; u4 parts[2]; } conv;

    ptr += idx;
    conv.parts[0] = ptr[0];
    conv.parts[1] = ptr[1];
    return conv.d;
#else
    double dval;
    memcpy(&dval, &ptr[idx], 8);
    return dval;
#endif
}

/* store a double into an array of u4 */
static inline void putDoubleToArray(u4 *ptr, int idx, double dval) {
#if defined(NO_UNALIGN_64__UNION)
    union { double d; u4 parts[2]; } conv;

    ptr += idx;
    conv.d = dval;
    ptr[0] = conv.parts[0];
    ptr[1] = conv.parts[1];
#else
    memcpy(&ptr[idx], &dval, 8);
#endif
}

//////////////////////////////////////////////////////////////////////////

//#define LOG_INSTR                   /* verbose debugging */
/* set and adjust ANDROID_LOG_TAGS='*:i jdwp:i dalvikvm:i dalvikvmi:i' */

/*
 * Export another copy of the PC on every instruction; this is largely
 * redundant with EXPORT_PC and the debugger code.  This value can be
 * compared against what we have stored on the stack with EXPORT_PC to
 * help ensure that we aren't missing any export calls.
 */
#if WITH_EXTRA_GC_CHECKS > 1
# define EXPORT_EXTRA_PC() (self->currentPc2 = pc)
#else
# define EXPORT_EXTRA_PC()
#endif

/*
 * Adjust the program counter.  "_offset" is a signed int, in 16-bit units.
 *
 * Assumes the existence of "const u2* pc" and "const u2* curMethod->insns".
 *
 * We don't advance the program counter until we finish an instruction or
 * branch, because we do want to have to unroll the PC if there's an
 * exception.
 */
#ifdef CHECK_BRANCH_OFFSETS
# define ADJUST_PC(_offset) do {                                            \
        int myoff = _offset;        /* deref only once */                   \
        if (pc + myoff < curMethod->insns ||                                \
            pc + myoff >= curMethod->insns + dvmGetMethodInsnsSize(curMethod)) \
        {                                                                   \
            char* desc;                                                     \
            desc = dexProtoCopyMethodDescriptor(&curMethod->prototype);     \
            MY_LOG_ERROR("Invalid branch %d at 0x%04x in %s.%s %s",                 \
                myoff, (int) (pc - curMethod->insns),                       \
                curMethod->clazz->descriptor, curMethod->name, desc);       \
            free(desc);                                                     \
            dvmAbort();                                                     \
        }                                                                   \
        pc += myoff;                                                        \
        EXPORT_EXTRA_PC();                                                  \
    } while (false)
#else
# define ADJUST_PC(_offset) do {                                            \
        pc += _offset;                                                      \
        EXPORT_EXTRA_PC();                                                  \
    } while (false)
#endif

/*
 * If enabled, validate the register number on every access.  Otherwise,
 * just do an array access.
 *
 * Assumes the existence of "u4* fp".
 *
 * "_idx" may be referenced more than once.
 */
#ifdef CHECK_REGISTER_INDICES
# define GET_REGISTER(_idx) \
    ( (_idx) < curMethod->registersSize ? \
        (fp[(_idx)]) : (assert(!"bad reg"),1969) )
# define SET_REGISTER(_idx, _val) \
    ( (_idx) < curMethod->registersSize ? \
        (fp[(_idx)] = (u4)(_val)) : (assert(!"bad reg"),1969) )
# define GET_REGISTER_AS_OBJECT(_idx)       ((Object *)GET_REGISTER(_idx))
# define SET_REGISTER_AS_OBJECT(_idx, _val) SET_REGISTER(_idx, (s4)_val)
# define GET_REGISTER_INT(_idx) ((s4) GET_REGISTER(_idx))
# define SET_REGISTER_INT(_idx, _val) SET_REGISTER(_idx, (s4)_val)
# define GET_REGISTER_WIDE(_idx) \
    ( (_idx) < curMethod->registersSize-1 ? \
        getLongFromArray(fp, (_idx)) : (assert(!"bad reg"),1969) )
# define SET_REGISTER_WIDE(_idx, _val) \
    ( (_idx) < curMethod->registersSize-1 ? \
        (void)putLongToArray(fp, (_idx), (_val)) : assert(!"bad reg") )
# define GET_REGISTER_FLOAT(_idx) \
    ( (_idx) < curMethod->registersSize ? \
        (*((float*) &fp[(_idx)])) : (assert(!"bad reg"),1969.0f) )
# define SET_REGISTER_FLOAT(_idx, _val) \
    ( (_idx) < curMethod->registersSize ? \
        (*((float*) &fp[(_idx)]) = (_val)) : (assert(!"bad reg"),1969.0f) )
# define GET_REGISTER_DOUBLE(_idx) \
    ( (_idx) < curMethod->registersSize-1 ? \
        getDoubleFromArray(fp, (_idx)) : (assert(!"bad reg"),1969.0) )
# define SET_REGISTER_DOUBLE(_idx, _val) \
    ( (_idx) < curMethod->registersSize-1 ? \
        (void)putDoubleToArray(fp, (_idx), (_val)) : assert(!"bad reg") )
#else
# define GET_REGISTER(_idx)                 (fp[(_idx)])
# define SET_REGISTER(_idx, _val)           (fp[(_idx)] = (_val))
# define GET_REGISTER_AS_OBJECT(_idx)       ((Object*) fp[(_idx)])
# define SET_REGISTER_AS_OBJECT(_idx, _val) (fp[(_idx)] = (u4)(_val))
# define GET_REGISTER_INT(_idx)             ((s4)GET_REGISTER(_idx))
# define SET_REGISTER_INT(_idx, _val)       SET_REGISTER(_idx, (s4)_val)
# define GET_REGISTER_WIDE(_idx)            getLongFromArray(fp, (_idx))
# define SET_REGISTER_WIDE(_idx, _val)      putLongToArray(fp, (_idx), (_val))
# define GET_REGISTER_FLOAT(_idx)           (*((float*) &fp[(_idx)]))
# define SET_REGISTER_FLOAT(_idx, _val)     (*((float*) &fp[(_idx)]) = (_val))
# define GET_REGISTER_DOUBLE(_idx)          getDoubleFromArray(fp, (_idx))
# define SET_REGISTER_DOUBLE(_idx, _val)    putDoubleToArray(fp, (_idx), (_val))
#endif

/*
 * Get 16 bits from the specified offset of the program counter.  We always
 * want to load 16 bits at a time from the instruction stream -- it's more
 * efficient than 8 and won't have the alignment problems that 32 might.
 *
 * Assumes existence of "const u2* pc".
 */
#define FETCH(_offset)     (pc[(_offset)])

/*
 * Extract instruction byte from 16-bit fetch (_inst is a u2).
 */
#define INST_INST(_inst)    ((_inst) & 0xff)//???????????

/*
 * Replace the opcode (used when handling breakpoints).  _opcode is a u1.
 */
#define INST_REPLACE_OP(_inst, _opcode) (((_inst) & 0xff00) | _opcode)

/*
 * Extract the "vA, vB" 4-bit registers from the instruction word (_inst is u2).
 */
#define INST_A(_inst)       (((_inst) >> 8) & 0x0f)
#define INST_B(_inst)       ((_inst) >> 12)

/*
 * Get the 8-bit "vAA" 8-bit register index from the instruction word.
 * (_inst is u2)
 */
#define INST_AA(_inst)      ((_inst) >> 8)

/*
 * The current PC must be available to Throwable constructors, e.g.
 * those created by the various exception throw routines, so that the
 * exception stack trace can be generated correctly.  If we don't do this,
 * the offset within the current method won't be shown correctly.  See the
 * notes in Exception.c.
 *
 * This is also used to determine the address for precise GC.
 *
 * Assumes existence of "u4* fp" and "const u2* pc".
 */
#define EXPORT_PC()         /*(SAVEAREA_FROM_FP(fp)->xtra.currentPc = pc)*/


/*
 * Check to see if "obj" is NULL.  If so, throw an exception.  Assumes the
 * pc has already been exported to the stack.
 *
 * Perform additional checks on debug builds.
 *
 * Use this to check for NULL when the instruction handler calls into
 * something that could throw an exception (so we have already called
 * EXPORT_PC at the top).
 */
static inline bool checkForNull(JNIEnv *env, jobject obj) {
    if (obj == NULL) {
        dvmThrowNullPointerException(env, NULL);
        return false;
    }
}

static inline bool checkForNull(JNIEnv *env, Object *obj) {
    if (obj == NULL) {
        dvmThrowNullPointerException(env, NULL);
        return false;
    }
#ifdef WITH_EXTRA_OBJECT_VALIDATION
    if (!dvmIsHeapAddress(obj)) {
        MY_LOG_ERROR("Invalid object %p", obj);
        dvmAbort();
    }
#endif
#ifndef NDEBUG
    if (obj == NULL) {
        /* probable heap corruption */
        MY_LOG_ERROR("Invalid object class %p (in %p)", obj, obj);
        dvmAbort();
    }
#endif
    return true;
}


/*
 * Check to see if "obj" is NULL.  If so, export the PC into the stack
 * frame and throw an exception.
 *
 * Perform additional checks on debug builds.
 *
 * Use this to check for NULL when the instruction handler doesn't do
 * anything else that can throw an exception.
 */
bool checkForNullExportPC(JNIEnv *env, Object *object) {
    if (object == NULL) {
        EXPORT_PC();
        dvmThrowNullPointerException(env, NULL);
        return false;
    }
    return true;
}

bool checkForNullExportPC(JNIEnv *env, jobject object) {
    if (object == NULL) {
        EXPORT_PC();
        dvmThrowNullPointerException(env, NULL);
        return false;
    }
    return true;
}

char *getRightClass(char *arraytrpe) {
    if ((0 == strncmp("L", arraytrpe, 1)) &&
        (0 == strncmp(";", arraytrpe + strlen(arraytrpe) - 1, 1))) {
        arraytrpe[strlen(arraytrpe) - 1] = '\0';
        arraytrpe = arraytrpe + 1;
        return arraytrpe;
    } else
        return arraytrpe;

}

char *getRight1Class(char *arraytrpe) {
    if ((0 == strncmp("L", arraytrpe, 1)) &&
        (0 == strncmp(";", arraytrpe + strlen(arraytrpe) - 1, 1))) {
        arraytrpe[strlen(arraytrpe) - 1] = '\0';
        arraytrpe = arraytrpe + 1;
        return arraytrpe;
    } else if (0 == strncmp("[I", arraytrpe, 2)) {
        int i = 4 + 1;
        return arraytrpe;
    } else
        return arraytrpe;
}
/* File: portable/stubdefs.cpp */
/*
 * In the C mterp stubs, "goto" is a function call followed immediately
 * by a return.
 */

#define GOTO_TARGET_DECL(_target, ...)

#define GOTO_TARGET(_target, ...) _target:

#define GOTO_TARGET_END

/* ugh */
#define STUB_HACK(x)
#define JIT_STUB_HACK(x)

/*
 * InterpSave's pc and fp must be valid when breaking out to a
 * "Reportxxx" routine.  Because the portable interpreter uses local
 * variables for these, we must flush prior.  Stubs, however, use
 * the interpSave vars directly, so this is a nop for stubs.
 */
#define PC_FP_TO_SELF()                                                    \
    self->interpSave.pc = pc;                                              \
    self->interpSave.curFrame = fp;
#define PC_TO_SELF() self->interpSave.pc = pc;

/*
 * Instruction framing.  For a switch-oriented implementation this is
 * case/break, for a threaded implementation it's a goto label and an
 * instruction fetch/computed goto.
 *
 * Assumes the existence of "const u2* pc" and (for threaded operation)
 * "u2 inst".
 */
//##????????????????????&&??????????????
# define H(_op)             &&op_##_op

# define HANDLE_OPCODE(_op) op_##_op:

# define FINISH(_offset) {                                                  \
        ADJUST_PC(_offset);                                                 \
        inst = FETCH(0);                                                    \
        goto *handlerTable[INST_INST(inst)];                                \
    }
# define FINISH_BKPT(_opcode) {                                             \
        goto *handlerTable[_opcode];                                        \
    }

#define OP_END

/*
 * The "goto" targets just turn into goto statements.  The "arguments" are
 * passed through local variables.
 */

#define GOTO_exceptionThrown() goto exceptionThrown;

//#define GOTO_returnFromMethod() goto returnFromMethod;

#define GOTO_invoke(_target, _methodCallRange)                              \
    do {                                                                    \
        methodCallRange = _methodCallRange;                                 \
        goto _target;                                                       \
    } while(false)

/* for this, the "args" are already in the locals */
#define GOTO_invokeMethod(_methodCallRange, _methodToCall, _vsrc1, _vdst) goto invokeMethod;

#define GOTO_bail() goto bail;


typedef struct typeArray {
    jarray marray;
    char *proto;
} typeArray;

typedef struct structObject {
    jobject obejct;
    char *proto;
    //char* arraytype;
} structObject;


typedef struct typeInvStatPar {

    char *proto;
} typeInvStatPar;
/*
 * �������Ƿ����
 *
 * While we're at it, see if a debugger has attached or the profiler has
 * started.  If so, switch to a different "goto" table.
 */
//#define PERIODIC_CHECKS(_pcadj) {                              \
//        if (dvmCheckSuspendQuick(self)) {                                   \
//            EXPORT_PC();  /* need for precise GC */                         \
//            dvmCheckSuspendPending(self);                                   \
//        }                                                                   \
//    }

/* File: c/opcommon.cpp */
/* forward declarations of goto targets */
// GOTO_TARGET_DECL(filledNewArray, bool methodCallRange);
// GOTO_TARGET_DECL(invokeVirtual, bool methodCallRange);
// GOTO_TARGET_DECL(invokeSuper, bool methodCallRange);
// GOTO_TARGET_DECL(invokeInterface, bool methodCallRange);
// GOTO_TARGET_DECL(invokeDirect, bool methodCallRange);
// GOTO_TARGET_DECL(invokeStatic, bool methodCallRange);
// GOTO_TARGET_DECL(invokeVirtualQuick, bool methodCallRange);
// GOTO_TARGET_DECL(invokeSuperQuick, bool methodCallRange);
// GOTO_TARGET_DECL(invokeMethod, bool methodCallRange, const Method* methodToCall,
//     u2 count, u2 regs);
// GOTO_TARGET_DECL(returnFromMethod);
// GOTO_TARGET_DECL(exceptionThrown);

/*
 * ===========================================================================
 *
 * What follows are opcode definitions shared between multiple opcodes with
 * minor substitutions handled by the C pre-processor.  These should probably
 * use the mterp substitution mechanism instead, with the code here moved
 * into common fragment files (like the asm "binop.S"), although it's hard
 * to give up the C preprocessor in favor of the much simpler text subst.
 *
 * ===========================================================================
 */

#define HANDLE_NUMCONV(_opcode, _opname, _fromtype, _totype)                \
    HANDLE_OPCODE(_opcode /*vA, vB*/)                                       \
        vdst = INST_A(inst);                                                \
        vsrc1 = INST_B(inst);                                               \
        MY_LOG_VERBOSE("|%s v%d,v%d", (_opname), vdst, vsrc1);              \
        SET_REGISTER##_totype(vdst,                                         \
            GET_REGISTER##_fromtype(vsrc1));                                \
        FINISH(1);

#define HANDLE_FLOAT_TO_INT(_opcode, _opname, _fromvtype, _fromrtype, \
        _tovtype, _tortype)                                                 \
    HANDLE_OPCODE(_opcode /*vA, vB*/)                                       \
    {                                                                       \
        /* spec defines specific handling for +/- inf and NaN values */     \
        _fromvtype val;                                                     \
        _tovtype intMin, intMax, result;                                    \
        vdst = INST_A(inst);                                                \
        vsrc1 = INST_B(inst);                                               \
        MY_LOG_VERBOSE("|%s v%d,v%d", (_opname), vdst, vsrc1);                       \
        val = GET_REGISTER##_fromrtype(vsrc1);                              \
        intMin = (_tovtype) 1 << (sizeof(_tovtype) * 8 -1);                 \
        intMax = ~intMin;                                                   \
        result = (_tovtype) val;                                            \
        if (val >= intMax)          /* +inf */                              \
            result = intMax;                                                \
        else if (val <= intMin)     /* -inf */                              \
            result = intMin;                                                \
        else if (val != val)        /* NaN */                               \
            result = 0;                                                     \
        else                                                                \
            result = (_tovtype) val;                                        \
        SET_REGISTER##_tortype(vdst, result);                               \
    }                                                                       \
    FINISH(1);

#define HANDLE_INT_TO_SMALL(_opcode, _opname, _type)                        \
    HANDLE_OPCODE(_opcode /*vA, vB*/)                                       \
        vdst = INST_A(inst);                                                \
        vsrc1 = INST_B(inst);                                               \
        MY_LOG_VERBOSE("|int-to-%s v%d,v%d", (_opname), vdst, vsrc1);                \
        SET_REGISTER(vdst, (_type) GET_REGISTER(vsrc1));                    \
        FINISH(1);

/* NOTE: the comparison result is always a signed 4-byte integer */
#define HANDLE_OP_CMPX(_opcode, _opname, _varType, _type, _nanVal)          \
    HANDLE_OPCODE(_opcode /*vAA, vBB, vCC*/)                                \
    {                                                                       \
        int result;                                                         \
        u2 regs;                                                            \
        _varType val1, val2;                                                \
        vdst = INST_AA(inst);                                               \
        regs = FETCH(1);                                                    \
        vsrc1 = regs & 0xff;                                                \
        vsrc2 = regs >> 8;                                                  \
        MY_LOG_VERBOSE("|cmp%s v%d,v%d,v%d", (_opname), vdst, vsrc1, vsrc2);         \
        val1 = GET_REGISTER##_type(vsrc1);                                  \
        val2 = GET_REGISTER##_type(vsrc2);                                  \
        if (val1 == val2)                                                   \
            result = 0;                                                     \
        else if (val1 < val2)                                               \
            result = -1;                                                    \
        else if (val1 > val2)                                               \
            result = 1;                                                     \
        else                                                                \
            result = (_nanVal);                                             \
        MY_LOG_VERBOSE("+ result=%d", result);                                       \
        SET_REGISTER(vdst, result);                                         \
    }                                                                       \
    FINISH(2);
#define HANDLE_OP_IF_XX(_opcode, _opname, _cmp)                             \
    HANDLE_OPCODE(_opcode /*vA, vB, +CCCC*/)                                \
        vsrc1 = INST_A(inst);                                               \
        vsrc2 = INST_B(inst);                                               \
        if ((s4) GET_REGISTER(vsrc1) _cmp (s4) GET_REGISTER(vsrc2)) {       \
            int branchOffset = (s2)FETCH(1);    /* sign-extended */         \
            MY_LOG_VERBOSE("|if-%s v%d,v%d,+0x%04x", (_opname), vsrc1, vsrc2,        \
                branchOffset);                                              \
            FINISH(branchOffset);                                           \
        } else {                                                            \
            MY_LOG_VERBOSE("|if-%s v%d,v%d,-", (_opname), vsrc1, vsrc2);             \
            FINISH(2);                                                      \
        }

#define HANDLE_OP_IF_XXZ(_opcode, _opname, _cmp)                            \
    HANDLE_OPCODE(_opcode /*vAA, +BBBB*/)                                   \
        vsrc1 = INST_AA(inst);                                              \
        if ((s4) GET_REGISTER(vsrc1) _cmp 0) {                              \
            int branchOffset = (s2)FETCH(1);    /* sign-extended */         \
            MY_LOG_VERBOSE("|if-%s v%d,+0x%04x", (_opname), vsrc1, branchOffset);\
            FINISH(branchOffset);                                           \
        } else {                                                            \
            MY_LOG_VERBOSE("|if-%s v%d,-", (_opname), vsrc1);                        \
            FINISH(2);                                                      \
        }

#define HANDLE_UNOP(_opcode, _opname, _pfx, _sfx, _type)                    \
    HANDLE_OPCODE(_opcode /*vA, vB*/)                                       \
        vdst = INST_A(inst);                                                \
        vsrc1 = INST_B(inst);                                               \
        MY_LOG_VERBOSE("|%s v%d,v%d", (_opname), vdst, vsrc1);                       \
        SET_REGISTER##_type(vdst, _pfx GET_REGISTER##_type(vsrc1) _sfx);    \
        FINISH(1);

#define HANDLE_OP_X_INT(_opcode, _opname, _op, _chkdiv)                     \
    HANDLE_OPCODE(_opcode /*vAA, vBB, vCC*/)                                \
    {                                                                       \
        u2 srcRegs;                                                         \
        vdst = INST_AA(inst);                                               \
        srcRegs = FETCH(1);                                                 \
        vsrc1 = srcRegs & 0xff;                                             \
        vsrc2 = srcRegs >> 8;                                               \
        MY_LOG_VERBOSE("|%s-int v%d,v%d", (_opname), vdst, vsrc1);                   \
        if (_chkdiv != 0) {                                                 \
            s4 firstVal, secondVal, result;                                 \
            firstVal = GET_REGISTER(vsrc1);                                 \
            secondVal = GET_REGISTER(vsrc2);                                \
            if (secondVal == 0) {                                           \
                EXPORT_PC();                                                \
                dvmThrowArithmeticException(env, "divide by zero");              \
                GOTO_exceptionThrown();                                     \
            }                                                               \
            if ((u4)firstVal == 0x80000000 && secondVal == -1) {            \
                if (_chkdiv == 1)                                           \
                    result = firstVal;  /* division */                      \
                else                                                        \
                    result = 0;         /* remainder */                     \
            } else {                                                        \
                result = firstVal _op secondVal;                            \
            }                                                               \
            SET_REGISTER(vdst, result);                                     \
        } else {                                                            \
            /* non-div/rem case */                                          \
            SET_REGISTER(vdst,                                              \
                (s4) GET_REGISTER(vsrc1) _op (s4) GET_REGISTER(vsrc2));     \
        }                                                                   \
    }                                                                       \
    FINISH(2);

#define HANDLE_OP_SHX_INT(_opcode, _opname, _cast, _op)                     \
    HANDLE_OPCODE(_opcode /*vAA, vBB, vCC*/)                                \
    {                                                                       \
        u2 srcRegs;                                                         \
        vdst = INST_AA(inst);                                               \
        srcRegs = FETCH(1);                                                 \
        vsrc1 = srcRegs & 0xff;                                             \
        vsrc2 = srcRegs >> 8;                                               \
        MY_LOG_VERBOSE("|%s-int v%d,v%d", (_opname), vdst, vsrc1);           \
        SET_REGISTER(vdst,                                                  \
            _cast GET_REGISTER(vsrc1) _op (GET_REGISTER(vsrc2) & 0x1f));    \
    }                                                                       \
    FINISH(2);

#define HANDLE_OP_X_INT_LIT16(_opcode, _opname, _op, _chkdiv)               \
    HANDLE_OPCODE(_opcode /*vA, vB, #+CCCC*/)                               \
        vdst = INST_A(inst);                                                \
        vsrc1 = INST_B(inst);                                               \
        vsrc2 = FETCH(1);                                                   \
        MY_LOG_VERBOSE("|%s-int/lit16 v%d,v%d,#+0x%04x",                             \
            (_opname), vdst, vsrc1, vsrc2);                                 \
        if (_chkdiv != 0) {                                                 \
            s4 firstVal, result;                                            \
            firstVal = GET_REGISTER(vsrc1);                                 \
            if ((s2) vsrc2 == 0) {                                          \
                EXPORT_PC();                                                \
                dvmThrowArithmeticException(env, "divide by zero");              \
                GOTO_exceptionThrown();                                     \
            }                                                               \
            if ((u4)firstVal == 0x80000000 && ((s2) vsrc2) == -1) {         \
                /* won't generate /lit16 instr for this; check anyway */    \
                if (_chkdiv == 1)                                           \
                    result = firstVal;  /* division */                      \
                else                                                        \
                    result = 0;         /* remainder */                     \
            } else {                                                        \
                result = firstVal _op (s2) vsrc2;                           \
            }                                                               \
            SET_REGISTER(vdst, result);                                     \
        } else {                                                            \
            /* non-div/rem case */                                          \
            SET_REGISTER(vdst, GET_REGISTER(vsrc1) _op (s2) vsrc2);         \
        }                                                                   \
        FINISH(2);

#define HANDLE_OP_X_INT_LIT8(_opcode, _opname, _op, _chkdiv)                \
    HANDLE_OPCODE(_opcode /*vAA, vBB, #+CC*/)                               \
    {                                                                       \
        u2 litInfo;                                                         \
        vdst = INST_AA(inst);                                               \
        litInfo = FETCH(1);                                                 \
        vsrc1 = litInfo & 0xff;                                             \
        vsrc2 = litInfo >> 8;       /* constant */                          \
        MY_LOG_VERBOSE("|%s-int/lit8 v%d,v%d,#+0x%02x",                              \
            (_opname), vdst, vsrc1, vsrc2);                                 \
        if (_chkdiv != 0) {                                                 \
            s4 firstVal, result;                                            \
            firstVal = GET_REGISTER(vsrc1);                                 \
            if ((s1) vsrc2 == 0) {                                          \
                EXPORT_PC();                                                \
                dvmThrowArithmeticException(env, "divide by zero");              \
                GOTO_exceptionThrown();                                     \
            }                                                               \
            if ((u4)firstVal == 0x80000000 && ((s1) vsrc2) == -1) {         \
                if (_chkdiv == 1)                                           \
                    result = firstVal;  /* division */                      \
                else                                                        \
                    result = 0;         /* remainder */                     \
            } else {                                                        \
                result = firstVal _op ((s1) vsrc2);                         \
            }                                                               \
            SET_REGISTER(vdst, result);                                     \
        } else {                                                            \
            SET_REGISTER(vdst,                                              \
                (s4) GET_REGISTER(vsrc1) _op (s1) vsrc2);                   \
        }                                                                   \
    }                                                                       \
    FINISH(2);

#define HANDLE_OP_SHX_INT_LIT8(_opcode, _opname, _cast, _op)                \
    HANDLE_OPCODE(_opcode /*vAA, vBB, #+CC*/)                               \
    {                                                                       \
        u2 litInfo;                                                         \
        vdst = INST_AA(inst);                                               \
        litInfo = FETCH(1);                                                 \
        vsrc1 = litInfo & 0xff;                                             \
        vsrc2 = litInfo >> 8;       /* constant */                          \
        MY_LOG_VERBOSE("|%s-int/lit8 v%d,v%d,#+0x%02x",                              \
            (_opname), vdst, vsrc1, vsrc2);                                 \
        SET_REGISTER(vdst,                                                  \
            _cast GET_REGISTER(vsrc1) _op (vsrc2 & 0x1f));                  \
    }                                                                       \
    FINISH(2);

#define HANDLE_OP_X_INT_2ADDR(_opcode, _opname, _op, _chkdiv)               \
    HANDLE_OPCODE(_opcode /*vA, vB*/)                                       \
        vdst = INST_A(inst);                                                \
        vsrc1 = INST_B(inst);                                               \
        MY_LOG_VERBOSE("|%s-int-2addr v%d,v%d", (_opname), vdst, vsrc1);             \
        if (_chkdiv != 0) {                                                 \
            s4 firstVal, secondVal, result;                                 \
            firstVal = GET_REGISTER(vdst);                                  \
            secondVal = GET_REGISTER(vsrc1);                                \
            if (secondVal == 0) {                                           \
                EXPORT_PC();                                                \
                dvmThrowArithmeticException(env, "divide by zero");              \
                GOTO_exceptionThrown();                                     \
            }                                                               \
            if ((u4)firstVal == 0x80000000 && secondVal == -1) {            \
                if (_chkdiv == 1)                                           \
                    result = firstVal;  /* division */                      \
                else                                                        \
                    result = 0;         /* remainder */                     \
            } else {                                                        \
                result = firstVal _op secondVal;                            \
            }                                                               \
            SET_REGISTER(vdst, result);                                     \
        } else {                                                            \
            SET_REGISTER(vdst,                                              \
                (s4) GET_REGISTER(vdst) _op (s4) GET_REGISTER(vsrc1));      \
        }                                                                   \
        FINISH(1);

#define HANDLE_OP_SHX_INT_2ADDR(_opcode, _opname, _cast, _op)               \
    HANDLE_OPCODE(_opcode /*vA, vB*/)                                       \
        vdst = INST_A(inst);                                                \
        vsrc1 = INST_B(inst);                                               \
        MY_LOG_VERBOSE("|%s-int-2addr v%d,v%d", (_opname), vdst, vsrc1);             \
        SET_REGISTER(vdst,                                                  \
            _cast GET_REGISTER(vdst) _op (GET_REGISTER(vsrc1) & 0x1f));     \
        FINISH(1);

#define HANDLE_OP_X_LONG(_opcode, _opname, _op, _chkdiv)                    \
    HANDLE_OPCODE(_opcode /*vAA, vBB, vCC*/)                                \
    {                                                                       \
        u2 srcRegs;                                                         \
        vdst = INST_AA(inst);                                               \
        srcRegs = FETCH(1);                                                 \
        vsrc1 = srcRegs & 0xff;                                             \
        vsrc2 = srcRegs >> 8;                                               \
        MY_LOG_VERBOSE("|%s-long v%d,v%d,v%d", (_opname), vdst, vsrc1, vsrc2);       \
        if (_chkdiv != 0) {                                                 \
            s8 firstVal, secondVal, result;                                 \
            firstVal = GET_REGISTER_WIDE(vsrc1);                            \
            secondVal = GET_REGISTER_WIDE(vsrc2);                           \
            if (secondVal == 0LL) {                                         \
                EXPORT_PC();                                                \
                dvmThrowArithmeticException(env, "divide by zero");              \
                GOTO_exceptionThrown();                                     \
            }                                                               \
            if ((u8)firstVal == 0x8000000000000000ULL &&                    \
                secondVal == -1LL)                                          \
            {                                                               \
                if (_chkdiv == 1)                                           \
                    result = firstVal;  /* division */                      \
                else                                                        \
                    result = 0;         /* remainder */                     \
            } else {                                                        \
                result = firstVal _op secondVal;                            \
            }                                                               \
            SET_REGISTER_WIDE(vdst, result);                                \
        } else {                                                            \
            SET_REGISTER_WIDE(vdst,                                         \
                (s8) GET_REGISTER_WIDE(vsrc1) _op (s8) GET_REGISTER_WIDE(vsrc2)); \
        }                                                                   \
    }                                                                       \
    FINISH(2);

#define HANDLE_OP_SHX_LONG(_opcode, _opname, _cast, _op)                    \
    HANDLE_OPCODE(_opcode /*vAA, vBB, vCC*/)                                \
    {                                                                       \
        u2 srcRegs;                                                         \
        vdst = INST_AA(inst);                                               \
        srcRegs = FETCH(1);                                                 \
        vsrc1 = srcRegs & 0xff;                                             \
        vsrc2 = srcRegs >> 8;                                               \
        MY_LOG_VERBOSE("|%s-long v%d,v%d,v%d", (_opname), vdst, vsrc1, vsrc2);       \
        SET_REGISTER_WIDE(vdst,                                             \
            _cast GET_REGISTER_WIDE(vsrc1) _op (GET_REGISTER(vsrc2) & 0x3f)); \
    }                                                                       \
    FINISH(2);

#define HANDLE_OP_X_LONG_2ADDR(_opcode, _opname, _op, _chkdiv)              \
    HANDLE_OPCODE(_opcode /*vA, vB*/)                                       \
        vdst = INST_A(inst);                                                \
        vsrc1 = INST_B(inst);                                               \
        MY_LOG_VERBOSE("|%s-long-2addr v%d,v%d", (_opname), vdst, vsrc1);            \
        if (_chkdiv != 0) {                                                 \
            s8 firstVal, secondVal, result;                                 \
            firstVal = GET_REGISTER_WIDE(vdst);                             \
            secondVal = GET_REGISTER_WIDE(vsrc1);                           \
            if (secondVal == 0LL) {                                         \
                EXPORT_PC();                                                \
                dvmThrowArithmeticException(env, "divide by zero");              \
                GOTO_exceptionThrown();                                     \
            }                                                               \
            if ((u8)firstVal == 0x8000000000000000ULL &&                    \
                secondVal == -1LL)                                          \
            {                                                               \
                if (_chkdiv == 1)                                           \
                    result = firstVal;  /* division */                      \
                else                                                        \
                    result = 0;         /* remainder */                     \
            } else {                                                        \
                result = firstVal _op secondVal;                            \
            }                                                               \
            SET_REGISTER_WIDE(vdst, result);                                \
        } else {                                                            \
            SET_REGISTER_WIDE(vdst,                                         \
                (s8) GET_REGISTER_WIDE(vdst) _op (s8)GET_REGISTER_WIDE(vsrc1));\
        }                                                                   \
        FINISH(1);

#define HANDLE_OP_SHX_LONG_2ADDR(_opcode, _opname, _cast, _op)              \
    HANDLE_OPCODE(_opcode /*vA, vB*/)                                       \
        vdst = INST_A(inst);                                                \
        vsrc1 = INST_B(inst);                                               \
        MY_LOG_VERBOSE("|%s-long-2addr v%d,v%d", (_opname), vdst, vsrc1);            \
        SET_REGISTER_WIDE(vdst,                                             \
            _cast GET_REGISTER_WIDE(vdst) _op (GET_REGISTER(vsrc1) & 0x3f)); \
        FINISH(1);

#define HANDLE_OP_X_FLOAT(_opcode, _opname, _op)                            \
    HANDLE_OPCODE(_opcode /*vAA, vBB, vCC*/)                                \
    {                                                                       \
        u2 srcRegs;                                                         \
        vdst = INST_AA(inst);                                               \
        srcRegs = FETCH(1);                                                 \
        vsrc1 = srcRegs & 0xff;                                             \
        vsrc2 = srcRegs >> 8;                                               \
        MY_LOG_VERBOSE("|%s-float v%d,v%d,v%d", (_opname), vdst, vsrc1, vsrc2);      \
        SET_REGISTER_FLOAT(vdst,                                            \
            GET_REGISTER_FLOAT(vsrc1) _op GET_REGISTER_FLOAT(vsrc2));       \
    }                                                                       \
    FINISH(2);

#define HANDLE_OP_X_DOUBLE(_opcode, _opname, _op)                           \
    HANDLE_OPCODE(_opcode /*vAA, vBB, vCC*/)                                \
    {                                                                       \
        u2 srcRegs;                                                         \
        vdst = INST_AA(inst);                                               \
        srcRegs = FETCH(1);                                                 \
        vsrc1 = srcRegs & 0xff;                                             \
        vsrc2 = srcRegs >> 8;                                               \
        MY_LOG_VERBOSE("|%s-double v%d,v%d,v%d", (_opname), vdst, vsrc1, vsrc2);     \
        SET_REGISTER_DOUBLE(vdst,                                           \
            GET_REGISTER_DOUBLE(vsrc1) _op GET_REGISTER_DOUBLE(vsrc2));     \
    }                                                                       \
    FINISH(2);

#define HANDLE_OP_X_FLOAT_2ADDR(_opcode, _opname, _op)                      \
    HANDLE_OPCODE(_opcode /*vA, vB*/)                                       \
        vdst = INST_A(inst);                                                \
        vsrc1 = INST_B(inst);                                               \
        MY_LOG_VERBOSE("|%s-float-2addr v%d,v%d", (_opname), vdst, vsrc1);           \
        SET_REGISTER_FLOAT(vdst,                                            \
            GET_REGISTER_FLOAT(vdst) _op GET_REGISTER_FLOAT(vsrc1));        \
        FINISH(1);

#define HANDLE_OP_X_DOUBLE_2ADDR(_opcode, _opname, _op)                     \
    HANDLE_OPCODE(_opcode /*vA, vB*/)                                       \
        vdst = INST_A(inst);                                                \
        vsrc1 = INST_B(inst);                                               \
        MY_LOG_VERBOSE("|%s-double-2addr v%d,v%d", (_opname), vdst, vsrc1);          \
        SET_REGISTER_DOUBLE(vdst,                                           \
            GET_REGISTER_DOUBLE(vdst) _op GET_REGISTER_DOUBLE(vsrc1));      \
        FINISH(1);
/////////////////////////////////////////////////////////////////////////

#define HANDLE_IGET_X(_opcode, _opname, _ftype, _regsize)                               \
    HANDLE_OPCODE(_opcode /*vA, vB, field@CCCC*/)                                       \
    {                                                                                   \
        vdst=INST_A(inst);                                                              \
        vsrc1=INST_B(inst);                                                             \
        orgref=FETCH(1);                                                                \
        curref=FETCH(2);                                                                \
        MY_LOG_VERBOSE("|iget%s v%d,v%d,field@0x%04x",(_opname), vdst, vsrc1, orgref);  \
        int fieldNameIdx =ycFile->mYcFormat.mFieldItem[curref].fieldNameIdx;            \
        MY_LOG_VERBOSE("|fieldNameIdx %d", fieldNameIdx);                               \
        int typeIdx=ycFile->mYcFormat.mFieldItem[curref].typeIdx;                       \
        MY_LOG_VERBOSE("|typeIdx %d", typeIdx);                                         \
        int classNameIdx=ycFile->mYcFormat.mFieldItem[curref].classNameIdx;             \
        MY_LOG_VERBOSE("|classNameIdx %d", classNameIdx);                               \
        char* className=ycFile->mYcFormat.mStringItem[classNameIdx];                    \
        MY_LOG_VERBOSE("|className %s", className);                                     \
        char* typeName=ycFile->mYcFormat.mStringItem[typeIdx];                          \
        MY_LOG_VERBOSE("|typeName %s", typeName);                                       \
        char* fieldName=ycFile->mYcFormat.mStringItem[fieldNameIdx];                    \
        MY_LOG_VERBOSE("|fieldName %s", fieldName);                                     \
        jclass jclass=env->FindClass(className);                                        \
          MY_LOG_VERBOSE("|jclass found ");                                             \
        jfieldID jfieldID=env->GetFieldID(jclass,fieldName,typeName);                   \
         MY_LOG_VERBOSE("|jfieldID found");                                             \
        if(OP_IGET==_opcode)                                                            \
        {                                                                               \
           jint mint=env->GetIntField(allobject.at(GET_REGISTER(vsrc1)).obejct,jfieldID);            \
           SET_REGISTER##_regsize(vdst,mint);                                             \
           MY_LOG_VERBOSE("|OP_IGET found");                                             \
       }                                                                                \
      if(OP_IGET_WIDE==_opcode){                                                        \
        jlong mlong=env->GetLongField(allobject.at(GET_REGISTER(vsrc1)).obejct,jfieldID);                                 \
        SET_REGISTER##_regsize(vdst,mlong);                                             \
        MY_LOG_VERBOSE("|OP_IGET_WIDE found");                                          \
        }                                                                               \
    if(OP_IGET_OBJECT==_opcode){\
        structObject thizObject;\
        thizObject.obejct = env->GetObjectField(allobject.at(GET_REGISTER(vsrc1)).obejct, jfieldID);\
        thizObject.proto = "L";\
        allobject.push_back(thizObject);\
        MY_LOG_VERBOSE("|GetObjectField successed");                                     \
        MY_LOG_VERBOSE("| mObject realloc successed");                                   \
        SET_REGISTER##_regsize(vdst,allobject.size()-1);                                      \
        MY_LOG_VERBOSE("|OP_IGET_OBJECT found");                                         \
    }                                                                                    \
    if(OP_IGET_BOOLEAN==_opcode){                                                        \
        jboolean mboolean=env->GetBooleanField(allobject.at(GET_REGISTER(vsrc1)).obejct,jfieldID);                   \
        SET_REGISTER##_regsize(vdst,mboolean);                                           \
        MY_LOG_VERBOSE("|OP_IGET_BOOLEAN found");                                        \
    }                                                                                    \
    if(OP_IGET_BYTE==_opcode){                                                           \
        jbyte mbyte=env->GetByteField(allobject.at(GET_REGISTER(vsrc1)).obejct,jfieldID);                                  \
        SET_REGISTER##_regsize(vdst,mbyte);                                              \
        MY_LOG_VERBOSE("|OP_IGET_BYTE found");                                           \
    }                                                                                    \
    if(OP_IGET_CHAR==_opcode){                                                           \
         jchar mchar=env->GetCharField(allobject.at(GET_REGISTER(vsrc1)).obejct,jfieldID);                                 \
        SET_REGISTER##_regsize(vdst,mchar);                                              \
        MY_LOG_VERBOSE("|OP_IGET_CHAR found");                                           \
    }                                                                                    \
    if(OP_IGET_SHORT==_opcode){                                                          \
        jboolean mshort=env->GetShortField(allobject.at(GET_REGISTER(vsrc1)).obejct,jfieldID);                             \
        SET_REGISTER##_regsize(vdst,mshort);                                            \
        MY_LOG_VERBOSE("|_opcode found  OP_IGET_SHORT");                                 \
    }                                                                                    \
    }                                                                                    \
    FINISH(3);
#define HANDLE_SGET_X(_opcode, _opname, _ftype, _regsize)                   \
    HANDLE_OPCODE(_opcode /*vAA, field@BBBB*/)                              \
    {                                                                       \
        vdst=INST_AA(inst);                                                  \
        orgref=FETCH(1);                                                    \
        curref=FETCH(2);                                                    \
        MY_LOG_VERBOSE("|sget%s v%d,sfield@0x%04x",(_opname),vdst, orgref);  \
        int fieldNameIdx =ycFile->mYcFormat.mFieldItem[curref].fieldNameIdx;\
        MY_LOG_VERBOSE("|fieldNameIdx %d", fieldNameIdx);                   \
        int typeIdx=ycFile->mYcFormat.mFieldItem[curref].typeIdx;           \
        MY_LOG_VERBOSE("|typeIdx %d", typeIdx);                             \
        int classNameIdx=ycFile->mYcFormat.mFieldItem[curref].classNameIdx; \
        MY_LOG_VERBOSE("|classNameIdx %d", classNameIdx);                    \
        char* className=ycFile->mYcFormat.mStringItem[classNameIdx];        \
         MY_LOG_VERBOSE("|className %s", className);                          \
        char* typeName=ycFile->mYcFormat.mStringItem[typeIdx];              \
         MY_LOG_VERBOSE("|typeName %s", typeName);                            \
        char* fieldName=ycFile->mYcFormat.mStringItem[fieldNameIdx];        \
         MY_LOG_VERBOSE("|fieldName %s", fieldName);                     \
        jclass jclass=env->FindClass(className);                            \
          MY_LOG_VERBOSE("|jclass found ");                                \
        jfieldID jfieldID=env->GetStaticFieldID(jclass,fieldName,typeName); \
          MY_LOG_VERBOSE("|jfieldID found");                                \
        if(_opcode==OP_SGET_OBJECT)                                         \
    {    \
        structObject thizObject;\
        thizObject.obejct =env->GetStaticObjectField(jclass,jfieldID);\
        thizObject.proto = "L";\
        allobject.push_back(thizObject);                                                                   \
        MY_LOG_VERBOSE("|GetObjectField successed");                        \
        MY_LOG_VERBOSE("| mObject realloc successed");                        \
        SET_REGISTER##_regsize(vdst,allobject.size()-1);                            \
    }                                                                           \
    if(_opcode==OP_SGET)                                                        \
    {                                                                           \
        jint mint=env->GetStaticIntField(jclass,jfieldID);                      \
        SET_REGISTER##_regsize(vdst,mint);                                      \
    }                                                                           \
    if(_opcode==OP_SGET_BOOLEAN)                                                \
    {                                                                           \
        jboolean mboolean=env->GetStaticBooleanField(jclass,jfieldID);          \
        SET_REGISTER##_regsize(vdst,mboolean);                                  \
    }                                                                           \
    if(_opcode==OP_SGET_BYTE)                                                   \
    {                                                                           \
        jbyte mbyte=env->GetStaticByteField(jclass,jfieldID);                   \
        SET_REGISTER##_regsize(vdst,mbyte);                                     \
    }                                                                           \
    if(_opcode==OP_SGET_CHAR)                                                   \
    {                                                                           \
        jchar mchar=env->GetStaticCharField(jclass,jfieldID);                   \
        SET_REGISTER##_regsize(vdst,mchar);                                     \
    }                                                                           \
    if(_opcode==OP_SGET_WIDE)                                                   \
    {                                                                           \
        jlong mlong=env->GetStaticLongField(jclass,jfieldID);                   \
        SET_REGISTER##_regsize(vdst,mlong);                                     \
    }                                                                           \
   }                                                                            \
   FINISH(3);
#define HANDLE_IPUT_X(_opcode, _opname, _ftype, _regsize)                   \
    HANDLE_OPCODE(_opcode /*vA, vB, field@CCCC*/)                           \
    {                                                                       \
        vdst=INST_A(inst);                                                  \
        vsrc1=INST_B(inst);                                                 \
        orgref=FETCH(1);                                                    \
        curref=FETCH(2);                                                    \
        MY_LOG_VERBOSE("|iput%s v%d,v%d,field@0x%04x", (_opname), vdst, vsrc1, orgref); \
        int classnameIdx= ycFile->mYcFormat.mFieldItem[curref].classNameIdx;\
        MY_LOG_VERBOSE("|classNameIdx %d", classnameIdx);                    \
        int typenameIdx=ycFile->mYcFormat.mFieldItem[curref].typeIdx;       \
        MY_LOG_VERBOSE("|typeIdx %d", typenameIdx);                             \
        int fieldnameIdx= ycFile->mYcFormat.mFieldItem[curref].fieldNameIdx;\
        MY_LOG_VERBOSE("|fieldNameIdx %d", fieldnameIdx);                   \
        char* classname=ycFile->mYcFormat.mStringItem[classnameIdx];        \
        MY_LOG_VERBOSE("|className %s", classname);                          \
        char* typeName=ycFile->mYcFormat.mStringItem[typenameIdx];          \
        MY_LOG_VERBOSE("|typeName %s", typeName);                            \
        char* fieldname=ycFile->mYcFormat.mStringItem[fieldnameIdx];        \
        MY_LOG_VERBOSE("|fieldName %s", fieldname);                          \
        jclass jclass1=env->FindClass(classname);                           \
        MY_LOG_VERBOSE("|jclass found ");                                \
        jfieldID jfieldID1=env->GetFieldID(jclass1,fieldname,typeName);     \
        MY_LOG_VERBOSE("|jfieldID found");                                \
        if(_opcode==OP_IPUT_OBJECT)                                        \
        {                                                                  \
            jobject value=allobject.at(GET_REGISTER##_regsize(vdst)).obejct;           \
            env->SetObjectField(allobject.at(GET_REGISTER(vsrc1)).obejct,jfieldID1,value);                 \
        }                                                                  \
        if(_opcode==OP_IPUT_BOOLEAN)                                       \
        {                                                                  \
            jboolean value =(jboolean)GET_REGISTER##_regsize(vdst);          \
            env->SetBooleanField(allobject.at(GET_REGISTER(vsrc1)).obejct,jfieldID1,value);                \
        }                                                                  \
        if(_opcode==OP_IPUT_BYTE)                                          \
        {                                                                  \
            jbyte value=(jbyte)GET_REGISTER##_regsize(vdst);                \
            env->SetByteField(allobject.at(GET_REGISTER(vsrc1)).obejct,jfieldID1,value);                   \
        }                                                                  \
        if(_opcode==OP_IPUT_CHAR)                                          \
        {                                                                  \
            jchar value=(jchar)GET_REGISTER##_regsize(vdst);                      \
            env->SetCharField(allobject.at(GET_REGISTER(vsrc1)).obejct,jfieldID1,value);                   \
        }                                                                  \
        if(_opcode==OP_IPUT_SHORT)                                         \
        {                                                                  \
            jshort value =(jshort)GET_REGISTER##_regsize(vdst);             \
            env->SetShortField(allobject.at(GET_REGISTER(vsrc1)).obejct,jfieldID1,value);                  \
        }                                                                  \
        if(_opcode==OP_IPUT_WIDE)                                          \
        {                                                                  \
            jlong value =GET_REGISTER##_regsize(vdst);                     \
            env->SetLongField(allobject.at(GET_REGISTER(vsrc1)).obejct,jfieldID1,value);                   \
        }                                                                  \
       if(_opcode==OP_IPUT_WIDE)                                          \
        {                                                                  \
            jint value =GET_REGISTER##_regsize(vdst);                     \
            env->SetIntField(allobject.at(GET_REGISTER(vsrc1)).obejct,jfieldID1,value);                   \
        }                                                                  \
}                                                                      \
    FINISH(3);

#define HANDLE_SPUT_X(_opcode, _opname, _ftype, _regsize)               \
    HANDLE_OPCODE(_opcode /*vAA, field@BBBB*/)                              \
    {                                                                       \
        vdst=INST_AA(inst);                                                 \
        orgref=FETCH(1);                                                    \
        curref=FETCH(2);                                                    \
        MY_LOG_VERBOSE("|sput%s v%d,sfield@0x%04x", (_opname), vdst, orgref);           \
        int classnameIdx= ycFile->mYcFormat.mFieldItem[curref].classNameIdx;\
        MY_LOG_VERBOSE("|classNameIdx %d", classnameIdx);                    \
        int typenameIdx=ycFile->mYcFormat.mFieldItem[curref].typeIdx;       \
        MY_LOG_VERBOSE("|typeIdx %d", typenameIdx);                             \
        int fieldnameIdx= ycFile->mYcFormat.mFieldItem[curref].fieldNameIdx;\
        MY_LOG_VERBOSE("|fieldNameIdx %d", fieldnameIdx);                   \
        char* classname=ycFile->mYcFormat.mStringItem[classnameIdx];        \
        MY_LOG_VERBOSE("|className %s", classname);                         \
        char* typeName=ycFile->mYcFormat.mStringItem[typenameIdx];          \
        MY_LOG_VERBOSE("|typeName %s", typeName);                            \
        char* fieldname=ycFile->mYcFormat.mStringItem[fieldnameIdx];        \
        MY_LOG_VERBOSE("|fieldName %s", fieldname);                        \
         jclass jclass1=env->FindClass(classname);                          \
        jfieldID jfieldID1=env->GetStaticFieldID(jclass1,fieldname,typeName);\
        if(_opcode==OP_SPUT)                                                \
        {                                                                   \
            jint value=GET_REGISTER(vdst);                        \
            env->SetStaticIntField(jclass1, jfieldID1, value);              \
        }                                                                   \
        if(_opcode==OP_SPUT_WIDE)                                           \
        {                                                                   \
            jlong value=(jlong)GET_REGISTER##_regsize(vdst);                       \
            env->SetStaticLongField(jclass1, jfieldID1, value);             \
        }                                                                   \
        if(_opcode==OP_SPUT_BOOLEAN)                                        \
        {                                                                   \
            jboolean value=(jboolean)GET_REGISTER(vdst);                    \
            env->SetStaticBooleanField(jclass1, jfieldID1, value);          \
        }                                                                   \
        if(_opcode==OP_SPUT_BYTE)                                           \
        {                                                                   \
           jbyte value=(jbyte)GET_REGISTER(vdst);                        \
            env->SetStaticByteField(jclass1, jfieldID1, value);             \
        }                                                                   \
        if(_opcode==OP_SPUT_CHAR)                                           \
        {                                                                   \
           jchar value=(jchar)GET_REGISTER(vdst);                  \
            env->SetStaticCharField(jclass1, jfieldID1, value);             \
        }                                                                   \
        if(_opcode==OP_SPUT_SHORT)                                          \
        {                                                                   \
            jshort value=(jshort)GET_REGISTER(vdst);              \
            env->SetStaticShortField(jclass1, jfieldID1, value);            \
        }                                                                   \
        if(_opcode==OP_SPUT_OBJECT)                                         \
        {                                                                   \
            jobject value=allobject.at(GET_REGISTER(vdst)).obejct;                      \
            env->SetStaticObjectField(jclass1, jfieldID1, value);           \
        }                                                                   \
    }                                                                           \
FINISH(3);

#define HANDLE_IGET_X_QUICK(_opcode, _opname, _ftype, _regsize)             \
    HANDLE_OPCODE(_opcode /*vA, vB, field@CCCC*/)                           \
    {                                                                       \
}                                                                         \
FINISH(2);
//        Object* obj;                                                        \
//        vdst = INST_A(inst);                                                \
//        vsrc1 = INST_B(inst);   /* object ptr */                            \
//        ref = FETCH(1);         /* field offset */                          \
//        ILOGV("|iget%s-quick v%d,v%d,field@+%u",                            \
//            (_opname), vdst, vsrc1, ref);                                   \
//        obj = (Object*) GET_REGISTER(vsrc1);                                \
//        if (!checkForNullExportPC(obj, fp, pc))                             \
//            GOTO_exceptionThrown();                                         \
//        SET_REGISTER##_regsize(vdst, dvmGetField##_ftype(obj, ref));        \
//        ILOGV("+ IGETQ %d=0x%08llx", ref,                                   \
//            (u8) GET_REGISTER##_regsize(vdst));                             \
//    }                                                                       \
  //  FINISH(2);


#define HANDLE_IPUT_X_QUICK(_opcode, _opname, _ftype, _regsize)             \
    HANDLE_OPCODE(_opcode /*vA, vB, field@CCCC*/)                           \
    {                                                                       \
}                                                                             \
FINISH(2);
//        Object* obj;                                                        \
//        vdst = INST_A(inst);                                                \
//        vsrc1 = INST_B(inst);   /* object ptr */                            \
//        ref = FETCH(1);         /* field offset */                          \
//        ILOGV("|iput%s-quick v%d,v%d,field@0x%04x",                         \
//            (_opname), vdst, vsrc1, ref);                                   \
//        obj = (Object*) GET_REGISTER(vsrc1);                                \
//        if (!checkForNullExportPC(obj, fp, pc))                             \
//            GOTO_exceptionThrown();                                         \
//        dvmSetField##_ftype(obj, ref, GET_REGISTER##_regsize(vdst));        \
//        ILOGV("+ IPUTQ %d=0x%08llx", ref,                                   \
//            (u8) GET_REGISTER##_regsize(vdst));                             \
//    }                                                                       \
 //   FINISH(2);

/*
 * The JIT needs dvmDexGetResolvedField() to return non-null.
 * Because the portable interpreter is not involved with the JIT
 * and trace building, we only need the extra check here when this
 * code is massaged into a stub called from an assembly interpreter.
 * This is controlled by the JIT_STUB_HACK maco.
 */




//////////////////////////////////////////////////////////////////////////

#define GOTO_bail() goto bail;

//////////////////////////////////////////////////////////////////////////
//�пɱ������������ȡ�����Ĳ���
jvalue NISLvmInterpretPortable(YcFile *ycFile, int i, JNIEnv *env, jobject thiz, ...) {
    jvalue *params = NULL; // �������顣
    struct value {
        jvalue val;
        char *proto;
    };
    value returnval;
//    jvalue retval;  // ����ֵ��
    jsize arrayLength; //��¼���鳤��
//    typeArray mtypeFillArray;//建立保存二维及以上数组的结构；

//    char *dyadicarrayProto;//存储二维数组的类型
    const u2 *pc;   // �����������
    u4 fp[65535];   // �Ĵ������顣
    u2 inst;        // ��ǰָ�
    u2 vsrc1, vsrc2, vdst;      // usually used for register indexes
    const SeparatorData *separatorData = ycFile->GetSeparatorData(i);
    u4 orgref, curref;

    // int mIntArraySize=0;
//    vector<jobject> mobject;
    vector<structObject> allobject;
    //vector<jclass> mClass;
    //vector<jstring> mJstring;


    //array����9�֡�

//    vector<typeArray> mtypeArray;
    //    vector<jarray> mArray;
//    vector<jbooleanArray> mBoolenArray;
//    vector<jbyteArray> mByteArray;
//    vector<jshortArray> mShortArray;
//    vector<jcharArray> mCharArray;
//    vector<jintArray> mIntArray;
//    vector<jlongArray> mLongArray;
//    vector<jfloatArray> mFloatArray;
//    vector<jdoubleArray> mDoubleArray;

//    jarray *mArray =(jarray*)malloc(sizeof(jarray));
//    int mArraySize=0;//??????????????????????????????
//    jintArray *mIntArray =(jintArray*)malloc(sizeof(jintArray));
    //    jclass *mClass =(jclass*)malloc(sizeof(jclass));
//    int mClassElemSize=0;//????????????????
//    jobject* mObject=(jobject*)malloc(sizeof(jobject));
//    int mObjectsize=0;
//    jstring * mJstring=(jstring*)malloc(sizeof(jstring));
//    int  mJstringsize=0;//????????????????

    unsigned int startIndex;

    // ���������
    va_list args;
    va_start(args, thiz);
    params = getParams(separatorData, args);
    va_end(args);

    // ��ò����Ĵ���������
    size_t paramRegCount = getParamRegCount(separatorData);

    // ���ò����Ĵ�����ֵ��
    if (isStaticMethod(separatorData)) {
        startIndex = separatorData->registerSize - separatorData->paramSize;//??static
    } else {
        startIndex = separatorData->registerSize - separatorData->paramSize;
        structObject thizObject;
        thizObject.obejct = thiz;
        allobject.push_back(thizObject);
        // mobject.push_back(thiz);
        // fp[startIndex - 1] = mobject.size() - 1;
        fp[startIndex - 1] = allobject.size() - 1;
    }
    for (int i = startIndex, j = 0; j < separatorData->paramSize; j++) {
        if ('D' == separatorData->paramShortDesc.str[i] ||
            'J' == separatorData->paramShortDesc.str[i]) {
            fp[i++] = params[j].j & 0xFFFFFFFF;
            fp[i++] = (params[j].j >> 32) & 0xFFFFFFFF;
        } else {
            fp[i++] = params[j].i;
        }
    }
    pc = separatorData->insts;
    /* static computed goto table */
    DEFINE_GOTO_TABLE(handlerTable);

    // ץȡ��һ��ָ�
    FINISH(0);

/* File: c/OP_NOP.cpp */
    HANDLE_OPCODE(OP_NOP)
    FINISH(1);
    OP_END

/* File: c/OP_MOVE.cpp */
    HANDLE_OPCODE(OP_MOVE /*vA, vB*/)
    vdst = INST_A(inst);
    vsrc1 = INST_B(inst);
    MY_LOG_VERBOSE("|move%s v%d,v%d %s(v%d=0x%08x)",
                   (INST_INST(inst) == OP_MOVE) ? "" : "-object", vdst, vsrc1,
                   kSpacing, vdst, GET_REGISTER(vsrc1));
    SET_REGISTER(vdst, GET_REGISTER(vsrc1));
    FINISH(1);
    OP_END

/* File: c/OP_MOVE_FROM16.cpp */
    HANDLE_OPCODE(OP_MOVE_FROM16 /*vAA, vBBBB*/)
    vdst = INST_AA(inst);
    vsrc1 = FETCH(1);
    MY_LOG_VERBOSE("|move%s/from16 v%d,v%d %s(v%d=0x%08x)",
                   (INST_INST(inst) == OP_MOVE_FROM16) ? "" : "-object", vdst, vsrc1,
                   kSpacing, vdst, GET_REGISTER(vsrc1));
    SET_REGISTER(vdst, GET_REGISTER(vsrc1));
    FINISH(2);
    OP_END

/* File: c/OP_MOVE_16.cpp */
    HANDLE_OPCODE(OP_MOVE_16 /*vAAAA, vBBBB*/)
    vdst = FETCH(1);
    vsrc1 = FETCH(2);
    MY_LOG_VERBOSE("|move%s/16 v%d,v%d %s(v%d=0x%08x)",
                   (INST_INST(inst) == OP_MOVE_16) ? "" : "-object", vdst, vsrc1,
                   kSpacing, vdst, GET_REGISTER(vsrc1));
    SET_REGISTER(vdst, GET_REGISTER(vsrc1));
    FINISH(3);
    OP_END

/* File: c/OP_MOVE_WIDE.cpp */
    HANDLE_OPCODE(OP_MOVE_WIDE /*vA, vB*/)
    /* IMPORTANT: must correctly handle overlapping registers, e.g. both
     * "move-wide v6, v7" and "move-wide v7, v6" */
    vdst = INST_A(inst);
    vsrc1 = INST_B(inst);
    MY_LOG_VERBOSE("|move-wide v%d,v%d %s(v%d=0x%08llx)", vdst, vsrc1,
                   kSpacing + 5, vdst, GET_REGISTER_WIDE(vsrc1));
    SET_REGISTER_WIDE(vdst, GET_REGISTER_WIDE(vsrc1));
    FINISH(1);
    OP_END

/* File: c/OP_MOVE_WIDE_FROM16.cpp */
    HANDLE_OPCODE(OP_MOVE_WIDE_FROM16 /*vAA, vBBBB*/)
    vdst = INST_AA(inst);
    vsrc1 = FETCH(1);
    MY_LOG_VERBOSE("|move-wide/from16 v%d,v%d  (v%d=0x%08llx)", vdst, vsrc1,
                   vdst, GET_REGISTER_WIDE(vsrc1));
    SET_REGISTER_WIDE(vdst, GET_REGISTER_WIDE(vsrc1));
    FINISH(2);
    OP_END
/* File: c/OP_MOVE_WIDE_16.cpp */
    HANDLE_OPCODE(OP_MOVE_WIDE_16 /*vAAAA, vBBBB*/)
    vdst = FETCH(1);
    vsrc1 = FETCH(2);
    MY_LOG_VERBOSE("|move-wide/16 v%d,v%d %s(v%d=0x%08llx)", vdst, vsrc1,
                   kSpacing + 8, vdst, GET_REGISTER_WIDE(vsrc1));
    SET_REGISTER_WIDE(vdst, GET_REGISTER_WIDE(vsrc1));
    FINISH(3);
    OP_END

/* File: c/OP_MOVE_OBJECT.cpp */
/* File: c/OP_MOVE.cpp */



    HANDLE_OPCODE(OP_MOVE_OBJECT /*vA, vB*/)
    vdst = INST_A(inst);
    vsrc1 = INST_B(inst);
    MY_LOG_VERBOSE("|move%s v%d,v%d %s(v%d=0x%08x)",
                   (INST_INST(inst) == OP_MOVE) ? "" : "-object", vdst, vsrc1,
                   kSpacing, vdst, GET_REGISTER(vsrc1));
    SET_REGISTER(vdst, GET_REGISTER(vsrc1));
    FINISH(1);
    OP_END


/* File: c/OP_MOVE_OBJECT_FROM16.cpp */
/* File: c/OP_MOVE_FROM16.cpp */
    HANDLE_OPCODE(OP_MOVE_OBJECT_FROM16 /*vAA, vBBBB*/)
    vdst = INST_AA(inst);
    vsrc1 = FETCH(1);
    MY_LOG_VERBOSE("|move%s/from16 v%d,v%d %s(v%d=0x%08x)",
                   (INST_INST(inst) == OP_MOVE_FROM16) ? "" : "-object", vdst, vsrc1,
                   kSpacing, vdst, GET_REGISTER(vsrc1));
    SET_REGISTER(vdst, GET_REGISTER(vsrc1));
    FINISH(2);
    OP_END


/* File: c/OP_MOVE_OBJECT_16.cpp */
/* File: c/OP_MOVE_16.cpp */
    HANDLE_OPCODE(OP_MOVE_OBJECT_16 /*vAAAA, vBBBB*/)
    vdst = FETCH(1);
    vsrc1 = FETCH(2);
    MY_LOG_VERBOSE("|move%s/16 v%d,v%d %s(v%d=0x%08x)",
                   (INST_INST(inst) == OP_MOVE_16) ? "" : "-object", vdst, vsrc1,
                   kSpacing, vdst, GET_REGISTER(vsrc1));
    SET_REGISTER(vdst, GET_REGISTER(vsrc1));
    FINISH(3);
    OP_END


/* File: c/OP_MOVE_RESULT.cpp */
    HANDLE_OPCODE(OP_MOVE_RESULT /*vAA*/)
    vdst = INST_AA(inst);
    MY_LOG_VERBOSE("|move-result%s v%d %s(v%d=0x%08x)",
                   (INST_INST(inst) == OP_MOVE_RESULT) ? "" : "-object",
                   vdst, kSpacing + 4, vdst, returnval.val.i);
    SET_REGISTER(vdst, returnval.val.i);
    FINISH(1);
    OP_END

/* File: c/OP_MOVE_RESULT_WIDE.cpp */
    HANDLE_OPCODE(OP_MOVE_RESULT_WIDE /*vAA*/)
    vdst = INST_AA(inst);
    MY_LOG_VERBOSE("|move-result-wide v%d %s(0x%08llx)", vdst, kSpacing, returnval.val.j);
    SET_REGISTER_WIDE(vdst, returnval.val.j);
    FINISH(1);
    OP_END

/* File: c/OP_MOVE_RESULT_OBJECT.cpp */
/* File: c/OP_MOVE_RESULT.cpp */
    HANDLE_OPCODE(OP_MOVE_RESULT_OBJECT /*vAA*/)
    vdst = INST_AA(inst);

    MY_LOG_VERBOSE("|move-result%s v%d %s(v%d=0x%08x)",
                   (INST_INST(inst) == OP_MOVE_RESULT) ? "" : "-object",
                   vdst, kSpacing + 4, vdst, returnval.val.i);
    //需要存储数据结构
//    mtypeArray.push_back(mtypeArray);
//    /mobject.push_back(retval.l);
    structObject thizObject;
    thizObject.obejct = returnval.val.l;
    thizObject.proto = returnval.proto;
    allobject.push_back(thizObject);
    SET_REGISTER(vdst, allobject.size() - 1);

    // SET_REGISTER(vdst, mobject.size() - 1);
//    mtypeArray.push_back(mtypeFillArray);
//    SET_REGISTER(vdst, mtypeArray.size() - 1);
    FINISH(1);
    OP_END



/* File: c/OP_MOVE_EXCEPTION.cpp */
    HANDLE_OPCODE(OP_MOVE_EXCEPTION /*vAA*/)
    {
        vdst = INST_AA(inst);
        MY_LOG_VERBOSE("|move-exception v%d", vdst);
        /*assert(self->exception != NULL);*/
        jthrowable exc;
        exc = (env)->ExceptionOccurred();

        if (exc) {
            structObject thizobject;
            thizobject.obejct = exc;
//            thizobject.proto = returnval.proto;
            allobject.push_back(thizobject);
            SET_REGISTER(vdst, allobject.size() - 1);
//            mobject.push_back(exc);
//            SET_REGISTER(vdst, mobject.size() - 1);
        } else {
            GOTO_exceptionThrown();
        }
    }
    FINISH(1);
    OP_END

/* File: c/OP_RETURN_VOID.cpp */
    HANDLE_OPCODE(OP_RETURN_VOID /**/)
    MY_LOG_VERBOSE("|return-void");
#ifndef NDEBUG
    returnval.val.j = 0xababababULL;    // placate valgrind
#endif
    /*GOTO_returnFromMethod();*/
    GOTO_bail();
    OP_END

/* File: c/OP_RETURN.cpp */
    HANDLE_OPCODE(OP_RETURN /*vAA*/)
    vsrc1 = INST_AA(inst);
    MY_LOG_VERBOSE("|return%s v%d",
                   (INST_INST(inst) == OP_RETURN) ? "" : "-object", vsrc1);
    returnval.val.i = GET_REGISTER(vsrc1);
    /*GOTO_returnFromMethod();*/
    GOTO_bail();
    OP_END

/* File: c/OP_RETURN_WIDE.cpp */
    HANDLE_OPCODE(OP_RETURN_WIDE /*vAA*/)
    vsrc1 = INST_AA(inst);
    MY_LOG_VERBOSE("|return-wide v%d", vsrc1);
    returnval.val.d = GET_REGISTER_DOUBLE(vsrc1);
    returnval.val.f = GET_REGISTER_FLOAT(vsrc1);
    returnval.val.j = GET_REGISTER_WIDE(vsrc1);
//    retval.d = GET_REGISTER_DOUBLE(vsrc1);
//    retval.f = GET_REGISTER_FLOAT(vsrc1);
//    retval.j = GET_REGISTER_WIDE(vsrc1);
    /*GOTO_returnFromMethod();*/
    GOTO_bail();
    OP_END

/* File: c/OP_RETURN_OBJECT.cpp */
/* File: c/OP_RETURN.cpp */
    HANDLE_OPCODE(OP_RETURN_OBJECT /*vAA*/)
    vsrc1 = INST_AA(inst);
    MY_LOG_VERBOSE("|return%s v%d",
                   (INST_INST(inst) == OP_RETURN) ? "" : "-object", vsrc1);

    returnval.val.l = allobject.at(GET_REGISTER(vsrc1)).obejct;
    //retval.i = GET_REGISTER(vsrc1);
    /*GOTO_returnFromMethod();*/
    GOTO_bail();
    OP_END


/* File: c/OP_CONST_4.cpp */
    HANDLE_OPCODE(OP_CONST_4 /*vA, #+B*/)
    {
        s4 tmp;

        vdst = INST_A(inst);
        tmp = (s4) (INST_B(inst) << 28) >> 28;  // sign extend 4-bit value
        MY_LOG_VERBOSE("|const/4 v%d,#0x%02x", vdst, (s4) tmp);
        SET_REGISTER(vdst, tmp);
    }
    FINISH(1);
    OP_END

/* File: c/OP_CONST_16.cpp */
    HANDLE_OPCODE(OP_CONST_16 /*vAA, #+BBBB*/)
    vdst = INST_AA(inst);
    vsrc1 = FETCH(1);
    MY_LOG_VERBOSE("|const/16 v%d,#0x%04x", vdst, (s2) vsrc1);
    SET_REGISTER(vdst, (s2) vsrc1);
    FINISH(2);
    OP_END

/* File: c/OP_CONST.cpp */
    HANDLE_OPCODE(OP_CONST /*vAA, #+BBBBBBBB*/)
    {
        u4 tmp;

        vdst = INST_AA(inst);
        tmp = FETCH(1);
        tmp |= (u4) FETCH(2) << 16;
        MY_LOG_VERBOSE("|const v%d,#0x%08x", vdst, tmp);
        SET_REGISTER(vdst, tmp);
    }
    FINISH(3);
    OP_END

/* File: c/OP_CONST_HIGH16.cpp */
    HANDLE_OPCODE(OP_CONST_HIGH16 /*vAA, #+BBBB0000*/)
    vdst = INST_AA(inst);
    vsrc1 = FETCH(1);
    MY_LOG_VERBOSE("|const/high16 v%d,#0x%04x0000", vdst, vsrc1);
    SET_REGISTER(vdst, vsrc1 << 16);
    FINISH(2);
    OP_END

/* File: c/OP_CONST_WIDE_16.cpp */
    HANDLE_OPCODE(OP_CONST_WIDE_16 /*vAA, #+BBBB*/)
    vdst = INST_AA(inst);
    vsrc1 = FETCH(1);
    MY_LOG_VERBOSE("|const-wide/16 v%d,#0x%04x", vdst, (s2) vsrc1);
    SET_REGISTER_WIDE(vdst, (s2) vsrc1);
    FINISH(2);
    OP_END

/* File: c/OP_CONST_WIDE_32.cpp */
    HANDLE_OPCODE(OP_CONST_WIDE_32 /*vAA, #+BBBBBBBB*/)
    {
        u4 tmp;

        vdst = INST_AA(inst);
        tmp = FETCH(1);
        tmp |= (u4) FETCH(2) << 16;
        MY_LOG_VERBOSE("|const-wide/32 v%d,#0x%08x", vdst, tmp);
        SET_REGISTER_WIDE(vdst, (s4) (size_t) tmp);
    }
    FINISH(3);
    OP_END

/* File: c/OP_CONST_WIDE.cpp */
    HANDLE_OPCODE(OP_CONST_WIDE /*vAA, #+BBBBBBBBBBBBBBBB*/)
    {
        u8 tmp;

        vdst = INST_AA(inst);
        tmp = FETCH(1);
        tmp |= (u8) FETCH(2) << 16;
        tmp |= (u8) FETCH(3) << 32;
        tmp |= (u8) FETCH(4) << 48;
        MY_LOG_VERBOSE("|const-wide v%d,#0x%08llx", vdst, tmp);
        SET_REGISTER_WIDE(vdst, tmp);
    }
    FINISH(5);
    OP_END

/* File: c/OP_CONST_WIDE_HIGH16.cpp */
    HANDLE_OPCODE(OP_CONST_WIDE_HIGH16 /*vAA, #+BBBB000000000000*/)
    vdst = INST_AA(inst);
    vsrc1 = FETCH(1);
    MY_LOG_VERBOSE("|const-wide/high16 v%d,#0x%04x000000000000", vdst, vsrc1);
    SET_REGISTER_WIDE(vdst, ((u8) vsrc1) << 48);
    FINISH(2);
    OP_END

//���ݶ���ָ��
    HANDLE_OPCODE(OP_CONST_STRING)
    {
        vdst = INST_AA(inst);
        orgref = FETCH(1);
        curref = FETCH(2);
        MY_LOG_VERBOSE("|const-string v%d string@0x%04x", vdst, orgref);
        char *str = ycFile->mYcFormat.mStringItem[curref];
        structObject thizobject;
        thizobject.obejct = env->NewStringUTF(str);
        allobject.push_back(thizobject);
        SET_REGISTER(vdst, allobject.size() - 1);
    }
    FINISH(3);
    OP_END
    // 和const-string是相似的，通过及字符串索引（较大），构造一个字符串，病赋值给寄存器vAA，使用2个shrot来存储索引位,即一个u4
    HANDLE_OPCODE(OP_CONST_STRING_JUMBO)
    {
        u4 tmp;
        vdst = INST_AA(inst);
        tmp = FETCH(1);
        tmp |= (u4) FETCH(2) << 16;
        MY_LOG_VERBOSE("|const-string v%d string@0x%08x", vdst, tmp);
        curref = FETCH(3);
        char *str = ycFile->mYcFormat.mStringItem[curref];
        structObject thizobject;
        thizobject.obejct = env->NewStringUTF(str);
        allobject.push_back(thizobject);
        SET_REGISTER(vdst, allobject.size() - 1);
    }
    FINISH(3 + 1)
    OP_END
    //通过类型索引获取一个类引用并赋给寄存器vAA。
    HANDLE_OPCODE(OP_CONST_CLASS)
    {
        vdst = INST_AA(inst);
        orgref = FETCH(1);
        curref = FETCH(2);
        MY_LOG_VERBOSE("|const-class v%d class@0x%04x", vdst, orgref);
        char *str = ycFile->mYcFormat.mStringItem[ycFile->mYcFormat.mTypeItem[curref]];
        structObject thizobject;
        thizobject.obejct = env->FindClass(str);
        allobject.push_back(thizobject);
        if (allobject.back().obejct == NULL)
            GOTO_exceptionThrown();
        SET_REGISTER(vdst, allobject.size() - 1);
    }
    FINISH(3)
    OP_END
//��ָ��
    HANDLE_OPCODE(OP_MONITOR_ENTER)
    {
        jobject obj;
        vsrc1 = INST_AA(inst);
        MY_LOG_VERBOSE("|monitor-enter v%d %s(0x%08x)",
                       vsrc1, kSpacing + 6, GET_REGISTER(vsrc1));
        obj = allobject.at(GET_REGISTER(vsrc1)).obejct;
        if (!checkForNullExportPC(env, obj))
            GOTO_exceptionThrown();
        env->MonitorEnter(obj);
    }
    HANDLE_OPCODE(OP_MONITOR_EXIT)
    {
        jobject obj;

        EXPORT_PC();

        vsrc1 = INST_AA(inst);
        MY_LOG_VERBOSE("|monitor-exit v%d %s(0x%08x)",
                       vsrc1, kSpacing + 5, GET_REGISTER(vsrc1));
        obj = allobject.at(GET_REGISTER(vsrc1)).obejct;
        if (!checkForNullExportPC(env, obj))
            GOTO_exceptionThrown();
        env->MonitorExit(obj);
    }
    FINISH(1);
    OP_END

    //ʵ������ָ��
    HANDLE_OPCODE(OP_CHECK_CAST)
    {
        jobject obj;
        jclass clazz;
        vsrc1 = INST_AA(inst); //��ת����obj
        orgref = FETCH(1);         /* class to check against */
        curref = FETCH(2);//��Ҫ��ת���ɵ���
        MY_LOG_VERBOSE("|check-cast v%d,class@0x%04x", vsrc1, orgref);
//        char *classtype = ycFile->mYcFormat.mStringItem[ycFile->mYcFormat.mTypeItem[orgref]];
        char *classtype = ycFile->mYcFormat.mStringItem[ycFile->mYcFormat.mTypeItem[curref]];
        //����Ҫ��ת���ɵ���
//        if (0 == strcmp(classtype, "[")) {
//        }
        classtype = getRightClass(classtype);
        clazz = env->FindClass(classtype);
        if (!env->IsInstanceOf(allobject.at(GET_REGISTER(vsrc1)).obejct, clazz)) {
            GOTO_exceptionThrown();
        } else {
            allobject.at(GET_REGISTER(vsrc1)).proto = classtype;

        }
//        if (!env->IsInstanceOf(mobject.at(GET_REGISTER(vsrc1)), clazz)) {
//            GOTO_exceptionThrown();
//        }
    }
    FINISH(3);
    OP_END

    HANDLE_OPCODE(OP_INSTANCE_OF)
    {
        jobject obj;
        jclass clazz;
        vdst = INST_A(inst);
        vsrc1 = INST_B(inst);   /* object to check */
        orgref = FETCH(1);         /* class to check against */
        curref = FETCH(2);
        char *classtype = ycFile->mYcFormat.mStringItem[ycFile->mYcFormat.mTypeItem[curref]];
        MY_LOG_VERBOSE("|instance-of v%d,v%d,class@0x%04x", vdst, vsrc1, orgref);
        obj = allobject.at(GET_REGISTER(vsrc1)).obejct;
        if (NULL == obj) {
            SET_REGISTER(vdst, 0);
        } else {
            //������
            classtype = getRightClass(classtype);
            clazz = env->FindClass(classtype);
            //determine whether obj is an instance of clazz;
            jboolean isInstance = env->IsInstanceOf(obj, clazz);

            SET_REGISTER(vdst, isInstance);
        }
    }
    FINISH(3);
    OP_END
//�������ָ��
    HANDLE_OPCODE(OP_ARRAY_LENGTH)
    {
        vdst = INST_A(inst);
        vsrc1 = INST_B(inst);
        MY_LOG_VERBOSE("|array-length v%d,v%d ", vdst, vsrc1);
        arrayLength = env->GetArrayLength((jarray) allobject.at(GET_REGISTER(vsrc1)).obejct);
        SET_REGISTER(vdst, arrayLength);
    }
    FINISH(1);
    OP_END
//   for(int i = 0;i<strlen(classname);i++){
//            if(*(classname+i)=='$'){
//                MY_LOG_VERBOSE("待保护方法中存在内部类，目前保护方案无法进行对其进行保护，因此造成的不便我们深感抱歉。后续将不断完善功能，谢谢！");
//                exit(0);
//            }
//        }
    HANDLE_OPCODE(OP_NEW_INSTANCE)
    {
        //��һ��Ӧ�ð�new-instance��invoke-directָ��һ��������
        vdst = INST_AA(inst);
        orgref = FETCH(1);
        curref = FETCH(2);
        MY_LOG_VERBOSE("|new-instance v%d,class@0x%04x", vdst, orgref);
        char *classname = ycFile->mYcFormat.mStringItem[ycFile->mYcFormat.mTypeItem[ycFile->mYcFormat.mMethodItem[curref].classnameIdx]];//Lcom/example/lz/advmptesteasy/MainActivity进入。

        jclass mclass = env->FindClass(classname);
        char *methodname = ycFile->mYcFormat.mStringItem[ycFile->mYcFormat.mMethodItem[curref].methodnameIdx];
        char *proto = ycFile->mYcFormat.mStringItem[ycFile->mYcFormat.mMethodItem[curref].protoIdx];
        jmethodID id_method = env->GetMethodID(mclass, methodname, proto);
        structObject thizobject;
        thizobject.obejct = env->NewObject(mclass, id_method);
        //必须要通过NewObject获取新对象，否则报错。
        allobject.push_back(thizobject);
        if (allobject.back().obejct == NULL)
            GOTO_exceptionThrown();
        // mClass=(jclass*)realloc(mClass,sizeof(jclass)*(mClassElemSize+1));//??????????????????????????????class
        SET_REGISTER(vdst, allobject.size() - 1);
    }
    FINISH(3);
    OP_END

    HANDLE_OPCODE(OP_NEW_ARRAY)
    {
        vdst = INST_A(inst);
        vsrc1 = INST_B(inst);//����
        orgref = FETCH(1);//��������
        curref = FETCH(2);//�������͵�����
        MY_LOG_VERBOSE("|new-array v%d,v%d,class@0x%04x  (%d elements)", vdst, vsrc1, orgref,
                       (s4) GET_REGISTER(vsrc1));
        s4 length = (s4) GET_REGISTER(vsrc1);
        char *type = ycFile->mYcFormat.mStringItem[curref];
        if (length < 0) {
            GOTO_exceptionThrown();
        }
        structObject thizobject;
        if (0 == strcmp(type, "[Z")) {
            thizobject.obejct = env->NewBooleanArray(length);
            thizobject.proto = "[Z";
            allobject.push_back(thizobject);
            SET_REGISTER(vdst, allobject.size() - 1);
        } else if (0 == strcmp(type, "[B")) {
            thizobject.obejct = env->NewByteArray(length);
            thizobject.proto = "[B";
            allobject.push_back(thizobject);
            SET_REGISTER(vdst, allobject.size() - 1);
        } else if (0 == strcmp(type, "[S")) {
            thizobject.obejct = env->NewShortArray(length);
            thizobject.proto = "[S";
            allobject.push_back(thizobject);
            SET_REGISTER(vdst, allobject.size() - 1);
        } else if (0 == strcmp(type, "[C")) {
            thizobject.obejct = env->NewCharArray(length);
            thizobject.proto = "[C";
            allobject.push_back(thizobject);
            SET_REGISTER(vdst, allobject.size() - 1);
        } else if (0 == strcmp(type, "[I")) {
            thizobject.obejct = env->NewIntArray(length);
            thizobject.proto = "[I";
            allobject.push_back(thizobject);
            SET_REGISTER(vdst, allobject.size() - 1);
        } else if (0 == strcmp(type, "[J")) {
            thizobject.obejct = env->NewLongArray(length);
            thizobject.proto = "[J";
            allobject.push_back(thizobject);
            SET_REGISTER(vdst, allobject.size() - 1);
        } else if (0 == strcmp(type, "[F")) {
            thizobject.obejct = env->NewFloatArray(length);
            thizobject.proto = "[F";
            allobject.push_back(thizobject);
            SET_REGISTER(vdst, allobject.size() - 1);
        } else if (0 == strcmp(type, "[D")) {
            thizobject.obejct = env->NewDoubleArray(length);
            thizobject.proto = "[D";
            allobject.push_back(thizobject);
            SET_REGISTER(vdst, allobject.size() - 1);
        } else {
            //char* arraytype=type+1;
            //Ӧ������type��Ԫ�ص���ȷ��
            char *arraytype = new char[strlen(type)];//���һλ������\0
            memset(arraytype, 0, strlen(type));
            strncpy(arraytype, type + 1, strlen(type) - 1);
            //�ٴ�У��classname�Ƿ���ȷ
            arraytype = getRightClass(arraytype);
            jclass arrayclass = env->FindClass(arraytype);
            if (NULL == arraytype)
                GOTO_exceptionThrown();
            thizobject.obejct = env->NewObjectArray(length, arrayclass, NULL);
            allobject.push_back(thizobject);
            SET_REGISTER(vdst, allobject.size() - 1);

        }

    }
    FINISH(3);
    OP_END
    //TODO {vC,vD,vF,vG},type@BBBB 构造指定类型（type@BBBB）和大小（vA）的数组并填充数组内容，vA寄存器是隐含使用的，除了指定数组的大小以外还指定了参数的个数
    //须配合move-result-object指令使用，参数列表为数组元素
    HANDLE_OPCODE(OP_FILLED_NEW_ARRAY)
    {
        u4 args5;
        jarray newarray;
        jobject newobject;
        orgref = FETCH(1);
        vdst = FETCH(2);
        curref = FETCH(3);
        args5 = INST_A(inst);
        vsrc1 = INST_B(inst);//指代寄存器的个数
        int args[vsrc1];//用来存放是第几寄存器
        if (vsrc1 < 5) {
            for (int i = 0; i < vsrc1; i++) {
                int t = (vdst >> (i * 4)) & 0xf;
                args[i] = (vdst >> (i * 4)) & 0xf;
            }
        } else {
            for (int i = 0; i < 4; i++) {
                args[i] = (vdst >> (i * 4)) & 0xf;
            }
            args[4] = args5;
        }

//        int test0 = GET_REGISTER(args[0]);
//        int test1 = GET_REGISTER(args[1]);
        s4 length = vsrc1;
//        typeArray typeArrayJudge;
        MY_LOG_VERBOSE("filled-new-array args=%d @0x%04x {regs=0x%04x %x}", vsrc1, orgref, vdst,
                       vsrc1 & 0x0f);
        //创建一个数组，并填充元素
        char *type = ycFile->mYcFormat.mStringItem[curref];//获取参数数组类型
//        int t =args[i];
        if (length < 0) {
            GOTO_exceptionThrown();
        }
        if (0 == strcmp(type, "[Z")) {
            jbooleanArray jarr = env->NewBooleanArray(length);
            //2.获取数组指针
            jboolean *arr = env->GetBooleanArrayElements(jarr, NULL);
            //3.赋值
            for (int i = 0; i < length; i++) {
                arr[i] = GET_REGISTER(args[i]);
            }
            //4.释放资源
            env->ReleaseBooleanArrayElements(jarr, arr, 0);
            //retval.l = jarr;
            returnval.val.l = jarr;
            returnval.proto = "[Z";
        } else if (0 == strcmp(type, "[B")) {
            jbyteArray jarr = env->NewByteArray(length);
            //2.获取数组指针
            jbyte *arr = env->GetByteArrayElements(jarr, NULL);
            //3.赋值
            for (int i = 0; i < length; i++) {
                arr[i] = GET_REGISTER(args[i]);
            }
            //4.释放资源
            env->ReleaseByteArrayElements(jarr, arr, 0);
            //retval.l = jarr;
            returnval.val.l = jarr;
            returnval.proto = "[B";
        } else if (0 == strcmp(type, "[S")) {
            jshortArray jarr = env->NewShortArray(length);
            //2.获取数组指针
            jshort *arr = env->GetShortArrayElements(jarr, NULL);
            //3.赋值
            for (int i = 0; i < length; i++) {
                arr[i] = GET_REGISTER(args[i]);
            }
            //4.释放资源
            env->ReleaseShortArrayElements(jarr, arr, 0);
            //retval.l = jarr;
            returnval.val.l = jarr;
            returnval.proto = "[S";
        } else if (0 == strcmp(type, "[C")) {
            jcharArray jarr = env->NewCharArray(length);
            //2.获取数组指针
            jchar *arr = env->GetCharArrayElements(jarr, NULL);
            //3.赋值
            for (int i = 0; i < length; i++) {
                arr[i] = GET_REGISTER(args[i]);
            }
            //4.释放资源
            env->ReleaseCharArrayElements(jarr, arr, 0);
            //retval.l = jarr;
            returnval.val.l = jarr;
            returnval.proto = "[C";
        } else if (0 == strcmp(type, "[I")) {
            //1.新建长度len数组
            jintArray jarr = env->NewIntArray(length);
            //2.获取数组指针
            jint *arr = env->GetIntArrayElements(jarr, NULL);
            //3.赋值
            for (int i = 0; i < length; i++) {
                arr[i] = GET_REGISTER(args[i]);
            }
            //4.释放资源
            env->ReleaseIntArrayElements(jarr, arr, 0);
            //retval.l = jarr;
            returnval.val.l = jarr;
            returnval.proto = "[I";
        } else if (0 == strcmp(type, "[J")) {
            //1.新建长度len数组
            jlongArray jarr = env->NewLongArray(length);
            //2.获取数组指针
            jlong *arr = env->GetLongArrayElements(jarr, NULL);
            //3.赋值
            for (int i = 0; i < length; i++) {
                arr[i] = GET_REGISTER(args[i]);
            }
            //4.释放资源
            env->ReleaseLongArrayElements(jarr, arr, 0);
            //retval.l = jarr;
            returnval.val.l = jarr;
            returnval.proto = "[J";
        } else if (0 == strcmp(type, "[F")) {
            //1.新建长度len数组
            jfloatArray jarr = env->NewFloatArray(length);
            //2.获取数组指针
            jfloat *arr = env->GetFloatArrayElements(jarr, NULL);
            //3.赋值
            for (int i = 0; i < length; i++) {
                arr[i] = GET_REGISTER(args[i]);
            }
            //4.释放资源
            env->ReleaseFloatArrayElements(jarr, arr, 0);
            //retval.l = jarr;
            returnval.val.l = jarr;
            returnval.proto = "[F";
        } else if (0 == strcmp(type, "[D")) {
            //1.新建长度len数组
            jdoubleArray jarr = env->NewDoubleArray(length);
            //2.获取数组指针
            jdouble *arr = env->GetDoubleArrayElements(jarr, NULL);
            //3.赋值
            for (int i = 0; i < length; i++) {
                arr[i] = GET_REGISTER(args[i]);
            }
            //4.释放资源
            env->ReleaseDoubleArrayElements(jarr, arr, 0);
            //retval.l = jarr;
            returnval.val.l = jarr;
            returnval.proto = "[D";
        } else {
            //char* arraytype=type+1;
            //有类类型么？
        }
    }

    //不需要存在寄存器中

    FINISH(3 + 1);
    OP_END
//    HANDLE_OPCODE(OP_FILLED_NEW_ARRAY)
//    {
//        u4 args5;
//        jarray newarray;
//        jobject newobject;
//        orgref = FETCH(1);
//        vdst = FETCH(2);
//        curref = FETCH(3);
//        args5 = INST_A(inst);
//        vsrc1 = INST_B(inst);//指代寄存器的个数
//        int args[vsrc1];//用来存放各个寄存器的
//        if (vsrc1 < 5) {
//            for (int i = 0; i < vsrc1; i++) {
//                args[i] = (vdst << i) & 0xf;
//            }
//        } else {
//            for (int i = 0; i < 4; i++) {
//                args[i] = (vdst >> (i * 4)) & 0xf;
//            }
//            args[5] = args5;
//        }
//
//        typeArray typeArrayJudge;
//        MY_LOG_VERBOSE("filled-new-array args=%d @0x%04x {regs=0x%04x %x}", vsrc1, orgref, vdst,
//                       vsrc1 & 0x0f);
//        //创建一个数组，并填充元素
//        char *type = ycFile->mYcFormat.mStringItem[curref];
//        jobjectArray result;
//        jarray tmp_array;
//        jobjectArray cur_array;
//        char *arrayproto = new char[vsrc1];
//        char *cur_proto = new char[100];//分配足够的空间
//        memset(cur_proto, 0x00, 100);
//        memcpy(cur_proto, type, strlen(type));//初始化
//        for (int i = vsrc1 - 1; i > 0; i--) {
//            if (i == vsrc1 - 1) {
//                jclass intArrcls = env->FindClass(cur_proto);
//                if (intArrcls == NULL) {
//                    GOTO_exceptionThrown();
//                }
//                cur_array = env->NewObjectArray(GET_REGISTER(args[i - 1]), intArrcls,
//                                                NULL);//初始值为NULL
//                if (0 == strcmp(type, "[Z")) {
//                    for (int j = 0; j < GET_REGISTER(args[i - 1]); j++)//该上维数组的长度为循环条件
//                    {
//                        jbooleanArray iarr = env->NewBooleanArray(
//                                GET_REGISTER(args[i])); //创建一维boolean数组
//                        if (iarr == NULL) {
//                            GOTO_exceptionThrown();
//                        }
//                        env->SetObjectArrayElement(cur_array, j, iarr);
//                        env->DeleteLocalRef(iarr);
//                    }
//
//                } else if (0 == strcmp(type, "[B")) {
//                    for (int j = 0; j < GET_REGISTER(args[i - 1]); j++)//该上维数组的长度为循环条件
//                    {
//                        jbyteArray iarr = env->NewByteArray(GET_REGISTER(args[i])); //创建一维byte数组
//                        if (iarr == NULL) {
//                            GOTO_exceptionThrown();
//                        }
//                        env->SetObjectArrayElement(cur_array, j, iarr);
//                        env->DeleteLocalRef(iarr);
//                    }
//
//                } else if (0 == strcmp(type, "[S")) {
//                    for (int j = 0; j < GET_REGISTER(args[i - 1]); j++)//该上维数组的长度为循环条件
//                    {
//                        jshortArray iarr = env->NewShortArray(GET_REGISTER(args[i])); //创建一维short数组
//                        if (iarr == NULL) {
//                            GOTO_exceptionThrown();
//                        }
//                        env->SetObjectArrayElement(cur_array, j, iarr);
//                        env->DeleteLocalRef(iarr);
//                    }
//
//                } else if (0 == strcmp(type, "[C")) {
//                    for (int j = 0; j < GET_REGISTER(args[i - 1]); j++)//该上维数组的长度为循环条件
//                    {
//                        jcharArray iarr = env->NewCharArray(GET_REGISTER(args[i])); //创建一维char数组
//                        if (iarr == NULL) {
//                            GOTO_exceptionThrown();
//                        }
//                        env->SetObjectArrayElement(cur_array, j, iarr);
//                        env->DeleteLocalRef(iarr);
//                    }
//
//                } else if (0 == strcmp(type, "[I")) {
//                    for (int j = 0; j < GET_REGISTER(args[i - 1]); j++)//该上维数组的长度为循环条件
//                    {
//                        jintArray iarr = env->NewIntArray(GET_REGISTER_INT(args[i])); //创建一维int数组
//                        if (iarr == NULL) {
//                            GOTO_exceptionThrown();
//                        }
//                        env->SetObjectArrayElement(cur_array, j, iarr);
//                        env->SetObjectArrayElement(cur_array, j, iarr);
//                        env->DeleteLocalRef(iarr);
//                    }
//
//                } else if (0 == strcmp(type, "[J")) {
//                    for (int j = 0; j < GET_REGISTER(args[i - 1]); j++)//该上维数组的长度为循环条件
//                    {
//                        jlongArray iarr = env->NewLongArray(
//                                GET_REGISTER_WIDE(args[i])); //创建一维long数组
//                        if (iarr == NULL) {
//                            GOTO_exceptionThrown();
//                        }
//                        env->SetObjectArrayElement(cur_array, j, iarr);
//                        env->DeleteLocalRef(iarr);
//                    }
//
//                } else if (0 == strcmp(type, "[F")) {
//                    for (int j = 0; j < GET_REGISTER(args[i - 1]); j++)//该上维数组的长度为循环条件
//                    {
//                        jfloatArray iarr = env->NewFloatArray(
//                                GET_REGISTER_FLOAT(args[i])); //创建一维float数组
//                        if (iarr == NULL) {
//                            GOTO_exceptionThrown();
//                        }
//                        env->SetObjectArrayElement(cur_array, j, iarr);
//                        env->DeleteLocalRef(iarr);
//                    }
//
//                } else if (0 == strcmp(type, "[D")) {
//                    for (int j = 0; j < GET_REGISTER(args[i - 1]); j++)//该上维数组的长度为循环条件
//                    {
//                        jdoubleArray iarr = env->NewDoubleArray(
//                                GET_REGISTER_DOUBLE(args[i])); //创建一维double数组
//                        if (iarr == NULL) {
//                            GOTO_exceptionThrown();
//                        }
//                        env->SetObjectArrayElement(cur_array, j, iarr);
//                        env->DeleteLocalRef(iarr);
//                    }
//
//                } else //类类型
//                {
//                    for (int j = 0; j < GET_REGISTER(args[i - 1]); j++)//该上维数组的长度为循环条件
//                    {
//                        char *arraytype = new char[strlen(type)];
//                        memset(arraytype, 0, strlen(type));
//                        strncpy(arraytype, type + 1, strlen(type) - 1);
//                        arraytype = getRightClass(arraytype);
//                        jclass arrayclass = env->FindClass(arraytype);
//                        if (NULL == arraytype)
//                            GOTO_exceptionThrown();
//                        jobjectArray iarr = env->NewObjectArray(GET_REGISTER(args[i]), arrayclass,
//                                                                NULL);
//                        if (iarr == NULL) {
//                            GOTO_exceptionThrown();
//                        }
//                        env->SetObjectArrayElement(cur_array, j, iarr);
//                        env->DeleteLocalRef(iarr);
//                    }
//
//                }
//                tmp_array = cur_array;
//            } else {
//                //首先创建上层维度的数组即为【【I
//                for (int k = strlen(cur_proto); k > 0; k--) {
//                    //cur_proto[k+1]='\0';
//                    cur_proto[k] = cur_proto[k - 1];
//                }
//                cur_proto[0] = '[';
//                jclass intArrcls = env->FindClass(cur_proto);
//                if (intArrcls == NULL) {
//                    GOTO_exceptionThrown();
//                }
//                int test = GET_REGISTER(args[i - 1]);
//                cur_array = env->NewObjectArray(GET_REGISTER(args[i - 1]), intArrcls,
//                                                NULL);//初始值为NULL
//                for (int j = 0; (j < GET_REGISTER(args[i - 1])) && ((i - 1) >= 0); j++) {
//                    env->SetObjectArrayElement(cur_array, j, tmp_array);
//                }
//                tmp_array = cur_array;
//
//            }
//        }
//        //不需要存在寄存器中
//        retval.l=tmp_array;
//    }
//    FINISH(3+1);
//    OP_END
//    //TODO 4

    HANDLE_OPCODE(OP_FILLED_NEW_ARRAY_RANGE)
    {
        u4 args5;
        jarray newarray;
        jobject newobject;
        orgref = FETCH(1);
        vdst = FETCH(2); //range base
        curref = FETCH(3);
        vsrc1 = INST_AA(inst);//指代寄存器的个数
        int args[vsrc1];//用来存放各个寄存器的
        for (int i = 0; i < vsrc1; i++) {
            args[i] = vdst + i;
        }
        typeArray typeArrayJudge;
        MY_LOG_VERBOSE("filled-new-array/range args=%d @0x%0x4x");
        //创建一个数组，并填充元素
        char *type = ycFile->mYcFormat.mStringItem[curref];
        s4 length = vsrc1;
//        typeArray typeArrayJudge;
        MY_LOG_VERBOSE("filled-new-array-range args=%d @0x%04x {regs=0x%04x %x}", vsrc1, orgref, vdst,
                       vsrc1 & 0x0f);
        //创建一个数组，并填充元素
        if (length < 0) {
            GOTO_exceptionThrown();
        }
        if (0 == strcmp(type, "[Z")) {
            jbooleanArray jarr = env->NewBooleanArray(length);
            //2.获取数组指针
            jboolean *arr = env->GetBooleanArrayElements(jarr, NULL);
            //3.赋值
            for (int i = 0; i < length; i++) {
                arr[i] = GET_REGISTER(args[i]);
            }
            //4.释放资源
            env->ReleaseBooleanArrayElements(jarr, arr, 0);
            //retval.l = jarr;
            returnval.val.l = jarr;
            returnval.proto = "[Z";
        } else if (0 == strcmp(type, "[B")) {
            jbyteArray jarr = env->NewByteArray(length);
            //2.获取数组指针
            jbyte *arr = env->GetByteArrayElements(jarr, NULL);
            //3.赋值
            for (int i = 0; i < length; i++) {
                arr[i] = GET_REGISTER(args[i]);
            }
            //4.释放资源
            env->ReleaseByteArrayElements(jarr, arr, 0);
            //retval.l = jarr;
            returnval.val.l = jarr;
            returnval.proto = "[B";
        } else if (0 == strcmp(type, "[S")) {
            jshortArray jarr = env->NewShortArray(length);
            //2.获取数组指针
            jshort *arr = env->GetShortArrayElements(jarr, NULL);
            //3.赋值
            for (int i = 0; i < length; i++) {
                arr[i] = GET_REGISTER(args[i]);
            }
            //4.释放资源
            env->ReleaseShortArrayElements(jarr, arr, 0);
            //retval.l = jarr;
            returnval.val.l = jarr;
            returnval.proto = "[S";
        } else if (0 == strcmp(type, "[C")) {
            jcharArray jarr = env->NewCharArray(length);
            //2.获取数组指针
            jchar *arr = env->GetCharArrayElements(jarr, NULL);
            //3.赋值
            for (int i = 0; i < length; i++) {
                arr[i] = GET_REGISTER(args[i]);
            }
            //4.释放资源
            env->ReleaseCharArrayElements(jarr, arr, 0);
            //retval.l = jarr;
            returnval.val.l = jarr;
            returnval.proto = "[C";
        } else if (0 == strcmp(type, "[I")) {
            //1.新建长度len数组
            jintArray jarr = env->NewIntArray(length);
            //2.获取数组指针
            jint *arr = env->GetIntArrayElements(jarr, NULL);
            //3.赋值
            for (int i = 0; i < length; i++) {
                arr[i] = GET_REGISTER(args[i]);
            }
            //4.释放资源
            env->ReleaseIntArrayElements(jarr, arr, 0);
            //retval.l = jarr;
            returnval.val.l = jarr;
            returnval.proto = "[I";
        } else if (0 == strcmp(type, "[J")) {
            //1.新建长度len数组
            jlongArray jarr = env->NewLongArray(length);
            //2.获取数组指针
            jlong *arr = env->GetLongArrayElements(jarr, NULL);
            //3.赋值
            for (int i = 0; i < length; i++) {
                arr[i] = GET_REGISTER(args[i]);
            }
            //4.释放资源
            env->ReleaseLongArrayElements(jarr, arr, 0);
            //retval.l = jarr;
            returnval.val.l = jarr;
            returnval.proto = "[J";
        } else if (0 == strcmp(type, "[F")) {
            //1.新建长度len数组
            jfloatArray jarr = env->NewFloatArray(length);
            //2.获取数组指针
            jfloat *arr = env->GetFloatArrayElements(jarr, NULL);
            //3.赋值
            for (int i = 0; i < length; i++) {
                arr[i] = GET_REGISTER(args[i]);
            }
            //4.释放资源
            env->ReleaseFloatArrayElements(jarr, arr, 0);
            //retval.l = jarr;
            returnval.val.l = jarr;
            returnval.proto = "[F";
        } else if (0 == strcmp(type, "[D")) {
            //1.新建长度len数组
            jdoubleArray jarr = env->NewDoubleArray(length);
            //2.获取数组指针
            jdouble *arr = env->GetDoubleArrayElements(jarr, NULL);
            //3.赋值
            for (int i = 0; i < length; i++) {
                arr[i] = GET_REGISTER(args[i]);
            }
            //4.释放资源
            env->ReleaseDoubleArrayElements(jarr, arr, 0);
            //retval.l = jarr;
            returnval.val.l = jarr;
            returnval.proto = "[D";
        } else {
            //char* arraytype=type+1;
            //有类类型么？
        }
    }
    FINISH(3 + 1);
    OP_END
//    HANDLE_OPCODE(OP_FILLED_NEW_ARRAY_RANGE)
//    {
//        u4 args5;
//        jarray newarray;
//        jobject newobject;
//        orgref = FETCH(1);
//        vdst = FETCH(2); //range base
//        curref = FETCH(3);
//        vsrc1 = INST_AA(inst);//指代寄存器的个数
//        int args[vsrc1];//用来存放各个寄存器的
//        for (int i = 0; i < vsrc1; i++) {
//            args[i] = vdst + i;
//        }
//        typeArray typeArrayJudge;
//        MY_LOG_VERBOSE("filled-new-array/range args=%d @0x%0x4x");
//        //创建一个数组，并填充元素
//        char *type = ycFile->mYcFormat.mStringItem[curref];
//        jobjectArray result;
//        jarray tmp_array;
//        jobjectArray cur_array;
//        char *arrayproto = new char[vsrc1];
//        char *cur_proto = new char[100];//分配足够的空间
//        memset(cur_proto, 0x00, 100);
//        memcpy(cur_proto, type, strlen(type));//初始化
//        for (int i = vsrc1 - 1; i > 0; i--) {
//            if (i == vsrc1 - 1) {
//                jclass intArrcls = env->FindClass(cur_proto);
//                if (intArrcls == NULL) {
//                    GOTO_exceptionThrown();
//                }
//                cur_array = env->NewObjectArray(GET_REGISTER(args[i - 1]), intArrcls,
//                                                NULL);//初始值为NULL
//                if (0 == strcmp(type, "[Z")) {
//                    for (int j = 0; j < GET_REGISTER(args[i - 1]); j++)//该上维数组的长度为循环条件
//                    {
//                        jbooleanArray iarr = env->NewBooleanArray(
//                                GET_REGISTER(args[i])); //创建一维boolean数组
//                        if (iarr == NULL) {
//                            GOTO_exceptionThrown();
//                        }
//                        env->SetObjectArrayElement(cur_array, j, iarr);
//                        env->DeleteLocalRef(iarr);
//                    }
//
//                } else if (0 == strcmp(type, "[B")) {
//                    for (int j = 0; j < GET_REGISTER(args[i - 1]); j++)//该上维数组的长度为循环条件
//                    {
//                        jbyteArray iarr = env->NewByteArray(GET_REGISTER(args[i])); //创建一维byte数组
//                        if (iarr == NULL) {
//                            GOTO_exceptionThrown();
//                        }
//                        env->SetObjectArrayElement(cur_array, j, iarr);
//                        env->DeleteLocalRef(iarr);
//                    }
//
//                } else if (0 == strcmp(type, "[S")) {
//                    for (int j = 0; j < GET_REGISTER(args[i - 1]); j++)//该上维数组的长度为循环条件
//                    {
//                        jshortArray iarr = env->NewShortArray(GET_REGISTER(args[i])); //创建一维short数组
//                        if (iarr == NULL) {
//                            GOTO_exceptionThrown();
//                        }
//                        env->SetObjectArrayElement(cur_array, j, iarr);
//                        env->DeleteLocalRef(iarr);
//                    }
//
//                } else if (0 == strcmp(type, "[C")) {
//                    for (int j = 0; j < GET_REGISTER(args[i - 1]); j++)//该上维数组的长度为循环条件
//                    {
//                        jcharArray iarr = env->NewCharArray(GET_REGISTER(args[i])); //创建一维char数组
//                        if (iarr == NULL) {
//                            GOTO_exceptionThrown();
//                        }
//                        env->SetObjectArrayElement(cur_array, j, iarr);
//                        env->DeleteLocalRef(iarr);
//                    }
//
//                } else if (0 == strcmp(type, "[I")) {
//                    for (int j = 0; j < GET_REGISTER(args[i - 1]); j++)//该上维数组的长度为循环条件
//                    {
//                        jintArray iarr = env->NewIntArray(GET_REGISTER_INT(args[i])); //创建一维int数组
//                        if (iarr == NULL) {
//                            GOTO_exceptionThrown();
//                        }
//                        env->SetObjectArrayElement(cur_array, j, iarr);
//                        env->DeleteLocalRef(iarr);
//                    }
//
//                } else if (0 == strcmp(type, "[J")) {
//                    for (int j = 0; j < GET_REGISTER(args[i - 1]); j++)//该上维数组的长度为循环条件
//                    {
//                        jlongArray iarr = env->NewLongArray(
//                                GET_REGISTER_WIDE(args[i])); //创建一维long数组
//                        if (iarr == NULL) {
//                            GOTO_exceptionThrown();
//                        }
//                        env->SetObjectArrayElement(cur_array, j, iarr);
//                        env->DeleteLocalRef(iarr);
//                    }
//
//                } else if (0 == strcmp(type, "[F")) {
//                    for (int j = 0; j < GET_REGISTER(args[i - 1]); j++)//该上维数组的长度为循环条件
//                    {
//                        jfloatArray iarr = env->NewFloatArray(
//                                GET_REGISTER_FLOAT(args[i])); //创建一维float数组
//                        if (iarr == NULL) {
//                            GOTO_exceptionThrown();
//                        }
//                        env->SetObjectArrayElement(cur_array, j, iarr);
//                        env->DeleteLocalRef(iarr);
//                    }
//
//                } else if (0 == strcmp(type, "[D")) {
//                    for (int j = 0; j < GET_REGISTER(args[i - 1]); j++)//该上维数组的长度为循环条件
//                    {
//                        jdoubleArray iarr = env->NewDoubleArray(
//                                GET_REGISTER_DOUBLE(args[i])); //创建一维double数组
//                        if (iarr == NULL) {
//                            GOTO_exceptionThrown();
//                        }
//                        env->SetObjectArrayElement(cur_array, j, iarr);
//                        env->DeleteLocalRef(iarr);
//                    }
//
//                } else //类类型
//                {
//                    for (int j = 0; j < GET_REGISTER(args[i - 1]); j++)//该上维数组的长度为循环条件
//                    {
//                        char *arraytype = new char[strlen(type)];
//                        memset(arraytype, 0, strlen(type));
//                        strncpy(arraytype, type + 1, strlen(type) - 1);
//                        arraytype = getRightClass(arraytype);
//                        jclass arrayclass = env->FindClass(arraytype);
//                        if (NULL == arraytype)
//                            GOTO_exceptionThrown();
//                        jobjectArray iarr = env->NewObjectArray(GET_REGISTER(args[i]), arrayclass,
//                                                                NULL);
//                        if (iarr == NULL) {
//                            GOTO_exceptionThrown();
//                        }
//                        env->SetObjectArrayElement(cur_array, j, iarr);
//                        env->DeleteLocalRef(iarr);
//                    }
//
//                }
//                tmp_array = cur_array;
//            } else {
//                //首先创建上层维度的数组即为【【I
//                for (int k = strlen(cur_proto); k > 0; k--) {
//                    //cur_proto[k+1]='\0';
//                    cur_proto[k] = cur_proto[k - 1];
//                }
//                cur_proto[0] = '[';
//                jclass intArrcls = env->FindClass(cur_proto);
//                if (intArrcls == NULL) {
//                    GOTO_exceptionThrown();
//                }
//                int test = GET_REGISTER(args[i - 1]);
//                cur_array = env->NewObjectArray(GET_REGISTER(args[i - 1]), intArrcls,
//                                                NULL);//初始值为NULL
//                for (int j = 0; (j < GET_REGISTER(args[i - 1])) && ((i - 1) >= 0); j++) {
//                    env->SetObjectArrayElement(cur_array, j, tmp_array);
//                }
//                tmp_array = cur_array;
//
//            }
//        }
//        //不需要存在寄存器中
//        retval.l = tmp_array;
//
//    }
//    FINISH(3 + 1);
//    OP_END
    HANDLE_OPCODE(OP_FILL_ARRAY_DATA)
    {

        s4 offset, tabletype, persize;
        vsrc1 = INST_AA(inst);//ָ���ڼ���mArry����
        u4 arrlocat = FETCH(1);//�õ�����ƫ��,�õ���14
        //�������� Table type: static array data
        persize = FETCH(arrlocat + 1);//���ÿ������Ĵ�С
        offset = FETCH(arrlocat + 2) | (((s4) FETCH(arrlocat + 2 + 1)) << 16);
        //��������С
        MY_LOG_VERBOSE("|fill-array-data v%d +0x%04x", vsrc1, offset);
        char *type = allobject.at(GET_REGISTER(vsrc1)).proto;
        //����int����
        if (0 == strcmp(type, "[I")) {
            jintArray arrayData;
            arrayData = (jintArray) allobject.at(GET_REGISTER(vsrc1)).obejct;
            arrayLength = env->GetArrayLength(
                    (jintArray) allobject.at(GET_REGISTER(vsrc1)).obejct);
            jint *carr = env->GetIntArrayElements(
                    (jintArray) allobject.at(GET_REGISTER(vsrc1)).obejct, NULL);

            for (int i = 0; i < arrayLength; i++) {
                carr[i] = FETCH(i * 2 + arrlocat + 2 + 2) |
                          (((s4) FETCH(i * 2 + arrlocat + 2 + 1 + 2)) << 16);
            }
            env->ReleaseIntArrayElements((jintArray) allobject.at(GET_REGISTER(vsrc1)).obejct,
                                         carr,
                                         0);
            jint *test = env->GetIntArrayElements(
                    (jintArray) allobject.at(GET_REGISTER(vsrc1)).obejct, NULL);
            for (int i = 0; i < arrayLength; i++) {
                int ccc = test[i];
                MY_LOG_VERBOSE("%d", ccc);
            }
        }
        // ����char����
        if (0 == strcmp(type, "[C")) {
            jcharArray arrayData;
            arrayData = (jcharArray) allobject.at(GET_REGISTER(vsrc1)).obejct;
            arrayLength = env->GetArrayLength(
                    (jcharArray) allobject.at(GET_REGISTER(vsrc1)).obejct);
            jchar *carr = env->GetCharArrayElements(
                    (jcharArray) allobject.at(GET_REGISTER(vsrc1)).obejct, NULL);

            for (int i = 0; i < arrayLength; i++) {
                carr[i] = FETCH(i + arrlocat + 2 + 2);
            }
            env->ReleaseCharArrayElements((jcharArray) allobject.at(GET_REGISTER(vsrc1)).obejct,
                                          carr, 0);
            jchar *test = env->GetCharArrayElements(
                    (jcharArray) allobject.at(GET_REGISTER(vsrc1)).obejct, NULL);
            for (int i = 0; i < arrayLength; i++) {
                char ccc = test[i];
                MY_LOG_VERBOSE("%d", ccc);
            }

        }
        //����short����
        if (0 == strcmp(type, "[S")) {
            jshortArray arrayData;
            arrayData = (jshortArray) allobject.at(GET_REGISTER(vsrc1)).obejct;
            arrayLength = env->GetArrayLength(
                    (jshortArray) allobject.at(GET_REGISTER(vsrc1)).obejct);
            jshort *carr = env->GetShortArrayElements(
                    (jshortArray) allobject.at(GET_REGISTER(vsrc1)).obejct,
                    NULL);

            for (int i = 0; i < arrayLength; i++) {
                carr[i] = FETCH(i + arrlocat + 2 + 2);
            }
            env->ReleaseShortArrayElements((jshortArray) allobject.at(GET_REGISTER(vsrc1)).obejct,
                                           carr, 0);
            jshort *test = env->GetShortArrayElements(
                    (jshortArray) allobject.at(GET_REGISTER(vsrc1)).obejct,
                    NULL);
            for (int i = 0; i < arrayLength; i++) {
                short ccc = test[i];
                MY_LOG_VERBOSE("%d", ccc);
            }
        }
        //����float����
        if (0 == strcmp(type, "[F")) {
            int tempfloat;
            jfloatArray arrayData;
            arrayData = (jfloatArray) allobject.at(GET_REGISTER(vsrc1)).obejct;
            arrayLength = env->GetArrayLength(
                    (jfloatArray) allobject.at(GET_REGISTER(vsrc1)).obejct);
            jfloat *carr = env->GetFloatArrayElements(
                    (jfloatArray) allobject.at(GET_REGISTER(vsrc1)).obejct,
                    NULL);

            for (int i = 0; i < arrayLength; i++) {
                tempfloat = 0;
                tempfloat = FETCH(i * 2 + arrlocat + 2 + 2) |
                            (((s4) FETCH(i * 2 + arrlocat + 2 + 1 + 2)) << 16);
                float d1;
                memcpy(&d1, &tempfloat, sizeof(tempfloat));
                carr[i] = d1;
                //������һ��float��������
//                float a = 0.0f;
//                unsigned char * b = (unsigned char*)&a;
//                int c[4];
//                c[0] = (FETCH(i * 2 + arrlocat + 2  + 2) >> 8) & 0xff;//cd
//                c[1] = FETCH(i * 2 + arrlocat + 2  + 2) & 0xff;//cc(zhengque)
//                c[2] = (FETCH(i * 2 + arrlocat + 2 + 1 + 2)>> 8) & 0xff;//4c
//                c[3] = FETCH(i * 2 + arrlocat + 2 + 1 + 2) & 0xff;//40(zhengque)
//
//                for (int i = 0; i<4; i++)
//                    b[i] = (unsigned char)c[i];
//                carr[i] = a;
            }
            env->ReleaseFloatArrayElements((jfloatArray) allobject.at(GET_REGISTER(vsrc1)).obejct,
                                           carr, 0);
            jfloat *test = env->GetFloatArrayElements(
                    (jfloatArray) allobject.at(GET_REGISTER(vsrc1)).obejct,
                    NULL);
            for (int i = 0; i < arrayLength; i++) {
                float ccc = test[i];
                MY_LOG_VERBOSE("%.2f", ccc);
            }
        }
        //����long����
        if (0 == strcmp(type, "[J")) {
            jlongArray arrayData;
            arrayData = (jlongArray) allobject.at(GET_REGISTER(vsrc1)).obejct;
            arrayLength = env->GetArrayLength(
                    (jlongArray) allobject.at(GET_REGISTER(vsrc1)).obejct);
            jlong *carr = env->GetLongArrayElements(
                    (jlongArray) allobject.at(GET_REGISTER(vsrc1)).obejct, NULL);

            for (int i = 0; i < arrayLength; i++) {
                carr[i] = FETCH(i * 4 + arrlocat + 2 + 2) |
                          (((s8) FETCH(i * 4 + arrlocat + 2 + 1 + 2)) << 16) |
                          (((s8) FETCH(i * 4 + arrlocat + 2 + 2 + 2)) << 32) |
                          (((s8) FETCH(i * 4 + arrlocat + 2 + 3 + 2)) << 48);
            }
            env->ReleaseLongArrayElements((jlongArray) allobject.at(GET_REGISTER(vsrc1)).obejct,
                                          carr, 0);
            jlong *test = env->GetLongArrayElements(
                    (jlongArray) allobject.at(GET_REGISTER(vsrc1)).obejct, NULL);
            for (int i = 0; i < arrayLength; i++) {
                long ccc = test[i];
                MY_LOG_VERBOSE("%d", ccc);
            }
        }
        //����double����
        if (0 == strcmp(type, "[D")) {
            jdoubleArray arrayData;
            arrayData = (jdoubleArray) allobject.at(GET_REGISTER(vsrc1)).obejct;
            arrayLength = env->GetArrayLength(
                    (jdoubleArray) allobject.at(GET_REGISTER(vsrc1)).obejct);
            jdouble *carr = env->GetDoubleArrayElements(
                    (jdoubleArray) allobject.at(GET_REGISTER(vsrc1)).obejct,
                    NULL);

            long long int tmp;

            for (int i = 0; i < arrayLength; i++) {
                tmp = 0;
                tmp = (((u8) FETCH(i * 4 + arrlocat + 2 + 2)) |
                       (((u8) FETCH(i * 4 + arrlocat + 2 + 1 + 2)) << 16) |
                       (((u8) FETCH(i * 4 + arrlocat + 2 + 2 + 2)) << 32) |
                       (((u8) FETCH(i * 4 + arrlocat + 2 + 3 + 2)) << 48));
                double d2;
                memcpy(&d2, &tmp, sizeof(tmp));
                carr[i] = d2;
                MY_LOG_VERBOSE("%.2f", carr[i]);
            }
            env->ReleaseDoubleArrayElements(
                    (jdoubleArray) allobject.at(GET_REGISTER(vsrc1)).obejct, carr, 0);
            jdouble *test = env->GetDoubleArrayElements(
                    (jdoubleArray) allobject.at(GET_REGISTER(vsrc1)).obejct,
                    NULL);
            for (int i = 0; i < arrayLength; i++) {
                double ccc = test[i];

            }
        }
        //����byte���ͣ�
        if (0 == strcmp(type, "[B")) {
            jbyteArray arrayData;
            arrayData = (jbyteArray) allobject.at(GET_REGISTER(vsrc1)).obejct;
            arrayLength = env->GetArrayLength(
                    (jbyteArray) allobject.at(GET_REGISTER(vsrc1)).obejct);
            jbyte *carr = env->GetByteArrayElements(
                    (jbyteArray) allobject.at(GET_REGISTER(vsrc1)).obejct,
                    NULL);

            int tmp;
            for (int i = 0; i < arrayLength; i++) {
                tmp = 0;
                if (i % 2 == 0) {
                    tmp = FETCH((int) (i / 2) + arrlocat + 2 + 2) & 0xff;
                } else {

                    tmp = (FETCH((int) (i / 2) + arrlocat + 2 + 2) >> 8);//????????
                }
                carr[i] = tmp;
            }
            env->ReleaseByteArrayElements((jbyteArray) allobject.at(GET_REGISTER(vsrc1)).obejct,
                                          carr, 0);
            jbyte *test = env->GetByteArrayElements(
                    (jbyteArray) allobject.at(GET_REGISTER(vsrc1)).obejct,
                    NULL);
            for (int i = 0; i < arrayLength; i++) {
                Byte ccc = test[i];
                MY_LOG_VERBOSE("%d", ccc);
            }
        }
    }
    FINISH(3)
    OP_END
//�쳣ָ��


//�쳣ָ��
    HANDLE_OPCODE(OP_THROW)
    {
        jthrowable obj;
        vsrc1 = INST_AA(inst);
        obj = (jthrowable) allobject.at(GET_REGISTER(vsrc1)).obejct;
        if (!checkForNull(env, obj)) {
            MY_LOG_VERBOSE("Bad exception");
        }
        if (obj) {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
        env->Throw(obj);
        //需要在抛出异常之前返回，否则函数会继续执行
        GOTO_bail();

    }

    OP_END
/* File: c/OP_GOTO.cpp */
//��תָ��
    HANDLE_OPCODE(OP_GOTO /*+AA*/)
    {
        vdst = INST_AA(inst);
        if ((s1) vdst < 0) {
            MY_LOG_VERBOSE("|goto -0x%02x", -((s1) vdst));
        } else {
            MY_LOG_VERBOSE("|goto +%02x", ((s1) vdst));
        }
        //MY_LOG_VERBOSE("> branch taken");
//    if ((s1)vdst < 0)
//        PERIODIC_CHECKS((s1)vdst);
        FINISH((s1) vdst);
    }

    OP_END
/* File: c/OP_GOTO_16.cpp */
    HANDLE_OPCODE(OP_GOTO_16 /*+AAAA*/)
    {
        s4 offset = (s2) FETCH(1);          /* sign-extend next code unit */
        MY_LOG_VERBOSE("|return-wide v%d", vsrc1);
        if (offset < 0) {
            MY_LOG_VERBOSE("|goto/16 -0x%04x", -offset);
        } else {
            MY_LOG_VERBOSE("|goto/16 +0x%04x", offset);
        }
        FINISH(offset);
    }

    OP_END

/* File: c/OP_GOTO_32.cpp */
    HANDLE_OPCODE(OP_GOTO_32 /*+AAAAAAAA*/)
    {
        s4 offset = FETCH(1);               /* low-order 16 bits */
        offset |= ((s4) FETCH(2)) << 16;    /* high-order 16 bits */

        if (offset < 0) {
            MY_LOG_VERBOSE("|goto/32 -0x%08x", -offset);
        } else {
            MY_LOG_VERBOSE("|goto/32 +0x%08x", offset);
        }
        FINISH(offset);
    }

    OP_END

/* File: c/OP_PACKED_SWITCH.cpp */
    HANDLE_OPCODE(OP_PACKED_SWITCH /*vAA, +BBBB*/)
    {
        const u2 *switchData;
        u4 testVal;
        s4 offset;
        vsrc1 = INST_AA(inst);
        offset = FETCH(1) | (((s4) FETCH(2)) << 16);
        MY_LOG_VERBOSE("|packed-switch v%d +0x%04x", vsrc1, offset);
        switchData = pc + offset;       // offset in 16-bit units
        testVal = GET_REGISTER(vsrc1);

        offset = dvmInterpHandlePackedSwitch(switchData, testVal);
        MY_LOG_VERBOSE("> branch taken (0x%04x)", offset);
        FINISH(offset);
    }

    OP_END

/* File: c/OP_SPARSE_SWITCH.cpp */
    HANDLE_OPCODE(OP_SPARSE_SWITCH /*vAA, +BBBB*/)
    {
        const u2 *switchData;
        u4 testVal;
        s4 offset;

        vsrc1 = INST_AA(inst);
        offset = FETCH(1) | (((s4) FETCH(2)) << 16);
        MY_LOG_VERBOSE("|sparse-switch v%d +0x%04x", vsrc1, offset);
        switchData = pc + offset;       // offset in 16-bit units
        testVal = GET_REGISTER(vsrc1);

        offset = dvmInterpHandleSparseSwitch(switchData, testVal);
        MY_LOG_VERBOSE("> branch taken (0x%04x)", offset);
        FINISH(offset);
    }

    OP_END


/* File: c/OP_CMPL_FLOAT.cpp */
HANDLE_OP_CMPX(OP_CMPL_FLOAT, "l-float", float, _FLOAT, -1)
    OP_END

/* File: c/OP_CMPG_FLOAT.cpp */
HANDLE_OP_CMPX(OP_CMPG_FLOAT, "g-float", float, _FLOAT, 1)
    OP_END




/* File: c/OP_CMPL_DOUBLE.cpp */
HANDLE_OP_CMPX(OP_CMPL_DOUBLE, "l-double", double, _DOUBLE, -1)
    OP_END

/* File: c/OP_CMPG_DOUBLE.cpp */
HANDLE_OP_CMPX(OP_CMPG_DOUBLE, "g-double", double, _DOUBLE, 1)
    OP_END

/* File: c/OP_CMP_LONG.cpp */
HANDLE_OP_CMPX(OP_CMP_LONG, "-long", s8, _WIDE, 0)
    OP_END

/* File: c/OP_IF_EQ.cpp */
HANDLE_OP_IF_XX(OP_IF_EQ, "eq", ==)
    OP_END

/* File: c/OP_IF_NE.cpp */
HANDLE_OP_IF_XX(OP_IF_NE, "ne", !=)
    OP_END

/* File: c/OP_IF_LT.cpp */
HANDLE_OP_IF_XX(OP_IF_LT, "lt", <)
    OP_END

/* File: c/OP_IF_GE.cpp */
HANDLE_OP_IF_XX(OP_IF_GE, "ge", >=)
    OP_END

/* File: c/OP_IF_GT.cpp */
HANDLE_OP_IF_XX(OP_IF_GT, "gt", >)
    OP_END

/* File: c/OP_IF_LE.cpp */
HANDLE_OP_IF_XX(OP_IF_LE, "le", <=)
    OP_END

/* File: c/OP_IF_EQZ.cpp */
HANDLE_OP_IF_XXZ(OP_IF_EQZ, "eqz", ==)
    OP_END
//
/* File: c/OP_IF_NEZ.cpp */
HANDLE_OP_IF_XXZ(OP_IF_NEZ, "nez", !=)
    OP_END

/* File: c/OP_IF_LTZ.cpp */
HANDLE_OP_IF_XXZ(OP_IF_LTZ, "ltz", <)
    OP_END

/* File: c/OP_IF_GEZ.cpp */
HANDLE_OP_IF_XXZ(OP_IF_GEZ, "gez", >=)
    OP_END

/* File: c/OP_IF_GTZ.cpp */
HANDLE_OP_IF_XXZ(OP_IF_GTZ, "gtz", >)
    OP_END

/* File: c/OP_IF_LEZ.cpp */
HANDLE_OP_IF_XXZ(OP_IF_LEZ, "lez", <=)
    OP_END
/* File: c/OP_UNUSED_3E.cpp */
    HANDLE_OPCODE(OP_UNUSED_3E)
    OP_END

/* File: c/OP_UNUSED_3F.cpp */
    HANDLE_OPCODE(OP_UNUSED_3F)
    OP_END

/* File: c/OP_UNUSED_40.cpp */
    HANDLE_OPCODE(OP_UNUSED_40)
    OP_END

/* File: c/OP_UNUSED_41.cpp */
    HANDLE_OPCODE(OP_UNUSED_41)
    OP_END

/* File: c/OP_UNUSED_42.cpp */
    HANDLE_OPCODE(OP_UNUSED_42)
    OP_END

/* File: c/OP_UNdiUSED_43.cpp */
    HANDLE_OPCODE(OP_UNUSED_43)
    OP_END

    HANDLE_OPCODE(OP_INVOKE_VIRTUAL)
    {
        jobject thisPtr;
        vsrc1 = INST_AA(inst);
        orgref = FETCH(1);
        vdst = FETCH(2);
        curref = FETCH(3);
//        assert((vsrc1 >> 4) > 0);
        MY_LOG_VERBOSE("|invoke-virtual args=%d @0x%04x {regs=0x%04x %x}",
                       vsrc1 >> 4, orgref, vdst, vsrc1 & 0x0f);
        char *classname = ycFile->mYcFormat.mStringItem[ycFile->mYcFormat.mTypeItem[ycFile->mYcFormat.mMethodItem[curref].classnameIdx]];
        jclass mclass = env->FindClass(classname);
        char *methodname = ycFile->mYcFormat.mStringItem[ycFile->mYcFormat.mMethodItem[curref].methodnameIdx];
        char *proto = ycFile->mYcFormat.mStringItem[ycFile->mYcFormat.mMethodItem[curref].protoIdx];
        char *proto1 = proto;
//        char *type111 = ycFile->mYcFormat.mStringItem[curref];
        jmethodID id_method = env->GetMethodID(mclass, methodname, proto);
        int args = (vsrc1 >> 4) - 1;
        jvalue reg[args];
        int test1 = vdst & 0xf;
        int counter = 1;
        while (counter <= args && *proto != NULL) {//对Proto中几个参数的类型进行判断并写入
            if (*proto == '(' || *proto == ';' || *proto == 'I' || *proto == 'Z' || *proto == 'B'
                || *proto == 'S' || *proto == 'C' || *proto == 'J' || *proto == 'D'|| *proto=='L') {
                switch (*++proto) {
                    case 'Z':
                        if (counter < 5) {
                            reg[counter - 1].z = GET_REGISTER((vdst >> (counter * 4)) & 0xf);
                        } else {
                            reg[counter - 1].z = GET_REGISTER(vsrc1 & 0xf);
                        }
                        counter++;
                        proto--;
                        break;
                    case 'B':
                        if (counter < 5) {
                            reg[counter - 1].b = GET_REGISTER((vdst >> (counter * 4)) & 0xf);
                        } else {
                            reg[counter - 1].b = GET_REGISTER(vsrc1 & 0xf);
                        }
                        counter++;
                        proto--;
                        break;
                    case 'S':
                        if (counter < 5) {
                            reg[counter - 1].s = GET_REGISTER((vdst >> (counter * 4)) & 0xf);
                        } else {
                            reg[counter - 1].s = GET_REGISTER(vsrc1 & 0xf);
                        }
                        counter++;
                        proto--;
                        break;
                    case 'C':
                        if (counter < 5) {
                            reg[counter - 1].c = GET_REGISTER((vdst >> (counter * 4)) & 0xf);
                        } else {
                            reg[counter - 1].c = GET_REGISTER(vsrc1 & 0xf);
                        }
                        counter++;
                        proto--;
                        break;
                    case 'I':
                        if (counter < 5) {
                            reg[counter - 1].i = GET_REGISTER((vdst >> (counter * 4)) & 0xf);
                        } else {
                            reg[counter - 1].i = GET_REGISTER(vsrc1 & 0xf);
                        }
                        counter++;
                        proto--;
                        break;
                    case 'J':
                        if (counter < 5) {
                            reg[counter - 1].j = GET_REGISTER((vdst >> (counter * 4)) & 0xf);
                        } else {
                            reg[counter - 1].j = GET_REGISTER(vsrc1 & 0xf);
                        }
                        counter++;
                        proto--;
                        break;
                    case 'F':
                        if (counter < 5) {
                            reg[counter - 1].f = GET_REGISTER((vdst >> (counter * 4)) & 0xf);
                        } else {
                            reg[counter - 1].f = GET_REGISTER(vsrc1 & 0xf);
                        }
                        counter++;
                        proto--;
                        break;
                    case 'D':
                        if (counter < 5) {
                            reg[counter - 1].d = GET_REGISTER((vdst >> (counter * 4)) & 0xf);
                        } else {
                            reg[counter - 1].d = GET_REGISTER(vsrc1 & 0xf);
                        }
                        counter++;
                        proto--;
                        break;
                    case '[':
                    case 'L':
                        if (counter < 5) {
                            reg[counter - 1].l = allobject.at(
                                    GET_REGISTER((vdst >> (counter * 4)) & 0xf)).obejct;
                        } else {
                            reg[counter - 1].l = allobject.at(GET_REGISTER(vsrc1 & 0xf)).obejct;
                        }
                        counter++;
                        proto--;
                        break;
                    default:
                        proto--;
                        break;
                }
            }
            proto++;
        }
//        if (args < 5) {//proto参数个数小于5，判别并获取寄存器内容。
//            for (int k = 0; k < args; k++) {
//                int jj = reg[k].i = GET_REGISTER((vdst >> (k * 4)) & 0xf);
//            }
//        } else {//proto参数个数等于5，判别并获取寄存器内容。
//            for (int k = 0; k < args - 1; k++) {
//                reg[k].i = GET_REGISTER((vdst >> (k * 4)) & 0xf);
//            }
//            reg[4].i = GET_REGISTER(vsrc1 & 0xf);
//        }
        //while(*(proto--)==')'
        while (true) {
            if (*proto1 == ')') {
                switch (*++proto1) {
                    case 'Z':
                        returnval.val.z = env->CallBooleanMethodA(
                                allobject.at(GET_REGISTER(vdst & 0x0f)).obejct,
                                id_method, reg);
                        break;
                    case 'B':
                        returnval.val.b = env->CallByteMethodA(
                                allobject.at(GET_REGISTER(vdst & 0x0f)).obejct,
                                id_method, reg);
                        break;
                    case 'S':
                        returnval.val.s = env->CallShortMethodA(
                                allobject.at(GET_REGISTER(vdst & 0x0f)).obejct,
                                id_method, reg);
                        break;
                    case 'C':
                        returnval.val.c = env->CallCharMethodA(
                                allobject.at(GET_REGISTER(vdst & 0x0f)).obejct,
                                id_method, reg);
                        break;
                    case 'I':
                        returnval.val.i = env->CallIntMethodA(
                                allobject.at(GET_REGISTER(vdst & 0x0f)).obejct,
                                id_method, reg);
                        break;
                    case 'J':
                        returnval.val.j = env->CallLongMethodA(
                                allobject.at(GET_REGISTER(vdst & 0x0f)).obejct,
                                id_method, reg);
                        break;
                    case 'D':
                        returnval.val.d = env->CallDoubleMethodA(
                                allobject.at(GET_REGISTER(vdst & 0x0f)).obejct,
                                id_method, reg);
                        break;
                    case '[':
                    case 'L':
                        returnval.val.l = env->CallObjectMethodA(
                                allobject.at(GET_REGISTER(vdst & 0x0f)).obejct,
                                id_method, reg);
                        break;
                    case 'V':
                    case '\0':
                        env->CallVoidMethodA(allobject.at(GET_REGISTER(vdst & 0x0f)).obejct,
                                             id_method, reg);
                        break;
                    default:
                        MY_LOG_WARNING("cannot get the currect type");
                        break;
                }
                break;
            }
            proto1++;
        }
        //retval.i=env->CallIntMethodA(mObject[GET_REGISTER(vdst & 0x0f)],id_method,reg);
    }
    FINISH(4);
    OP_END

    HANDLE_OPCODE(OP_INVOKE_SUPER)
    {
        jobject *thisPtr;
        vsrc1 = INST_AA(inst);
        orgref = FETCH(1);
        vdst = FETCH(2);
        curref = FETCH(3);
//        assert((vsrc1 >> 4) > 0);
        MY_LOG_VERBOSE("|invoke-super args=%d @0x%04x {regs=0x%04x %x}",
                       vsrc1 >> 4, orgref, vdst, vsrc1 & 0x0f);
        //��������
        char *classname = ycFile->mYcFormat.mStringItem[ycFile->mYcFormat.mTypeItem[ycFile->mYcFormat.mMethodItem[curref].classnameIdx]];

//        char *classname = ycFile->mYcFormat.mStringItem[ycFile->mYcFormat.mTypeItem[ycFile->mYcFormat.mMethodItem->classnameIdx]]; char *classname = ycFile->mYcFormat.mStringItem[ycFile->mYcFormat.mTypeItem[ycFile->mYcFormat.mMethodItem->classnameIdx]];
        jclass mclass = env->FindClass(classname);
        char *methodname = ycFile->mYcFormat.mStringItem[ycFile->mYcFormat.mMethodItem[curref].methodnameIdx];
        char *proto = ycFile->mYcFormat.mStringItem[ycFile->mYcFormat.mMethodItem[curref].protoIdx];
        char *proto1 = proto;
        jmethodID id_method = env->GetMethodID(mclass, methodname, proto);
        int args = (vsrc1 >> 4) - 1;
        jvalue reg[args];
        int counter = 1;
        while (counter <= args && *proto != NULL) {//对Proto中几个参数的类型进行判断并写入
            if (*proto == '(' || *proto == ';' || *proto == 'I' || *proto == 'Z' || *proto == 'B'
                || *proto == 'S' || *proto == 'C' || *proto == 'J' || *proto == 'D') {
                switch (*++proto) {
                    case 'Z':
                        if (counter < 5) {
                            reg[counter-1].z = GET_REGISTER((vdst >> (counter * 4)) & 0xf);
                        } else {
                            reg[counter-1].z = GET_REGISTER(vsrc1 & 0xf);
                        }
                        counter++;
                        proto--;
                        break;
                    case 'B':
                        if (counter < 5) {
                            reg[counter-1].b = GET_REGISTER((vdst >> (counter * 4)) & 0xf);
                        } else {
                            reg[counter-1].b = GET_REGISTER(vsrc1 & 0xf);
                        }
                        counter++;
                        proto--;
                        break;
                    case 'S':
                        if (counter < 5) {
                            reg[counter-1].s = GET_REGISTER((vdst >> (counter * 4)) & 0xf);
                        } else {
                            reg[counter-1].s = GET_REGISTER(vsrc1 & 0xf);
                        }
                        counter++;
                        proto--;
                        break;
                    case 'C':
                        if (counter < 5) {
                            reg[counter-1].c = GET_REGISTER((vdst >> (counter * 4)) & 0xf);
                        } else {
                            reg[counter-1].c = GET_REGISTER(vsrc1 & 0xf);
                        }
                        counter++;
                        proto--;
                        break;
                    case 'I':
                        if (counter < 5) {
                            reg[counter-1].i = GET_REGISTER((vdst >> (counter * 4)) & 0xf);
                        } else {
                            reg[counter-1].i = GET_REGISTER(vsrc1 & 0xf);
                        }
                        counter++;
                        proto--;
                        break;
                    case 'J':
                        if (counter < 5) {
                            reg[counter-1].j = GET_REGISTER((vdst >> (counter * 4)) & 0xf);
                        } else {
                            reg[counter-1].j = GET_REGISTER(vsrc1 & 0xf);
                        }
                        counter++;
                        proto--;
                        break;
                    case 'F':
                        if (counter < 5) {
                            reg[counter-1].f = GET_REGISTER((vdst >> (counter * 4)) & 0xf);
                        } else {
                            reg[counter-1].f = GET_REGISTER(vsrc1 & 0xf);
                        }
                        counter++;
                        proto--;
                        break;
                    case 'D':
                        if (counter < 5) {
                            reg[counter-1].d = GET_REGISTER((vdst >> (counter * 4)) & 0xf);
                        } else {
                            reg[counter-1].d = GET_REGISTER(vsrc1 & 0xf);
                        }
                        counter++;
                        proto--;
                        break;
                    case '[':
                    case 'L':
                        if (counter < 5) {
                            reg[counter-1].l = allobject.at(
                                    GET_REGISTER((vdst >> (counter * 4)) & 0xf)).obejct;
                        } else {
                            reg[counter-1].l = allobject.at(GET_REGISTER(vsrc1 & 0xf)).obejct;
                        }
                        counter++;
                        proto--;
                        break;
                    default:
                        proto--;
                        break;
                }
            }
            proto++;
        }
        //env->CallNonvirtualVoidMethodA(mObject[GET_REGISTER(vdst&0x0f)],mclass,id_method,reg);
        //while(*(proto--)==')'
        while (true) {
            if (*proto1 == ')') {
                switch (*++proto1) {
                    case 'Z':
                        returnval.val.z = env->CallNonvirtualBooleanMethodA(
                                allobject.at(GET_REGISTER(vdst & 0x0f)).obejct, mclass, id_method,
                                reg);
                        break;
                    case 'B':
                        returnval.val.b = env->CallNonvirtualByteMethodA(
                                allobject.at(GET_REGISTER(vdst & 0x0f)).obejct, mclass, id_method,
                                reg);
                        break;
                    case 'S':
                        returnval.val.s = env->CallNonvirtualShortMethodA(
                                allobject.at(GET_REGISTER(vdst & 0x0f)).obejct, mclass, id_method,
                                reg);
                        break;
                    case 'C':
                        returnval.val.c = env->CallNonvirtualCharMethodA(
                                allobject.at(GET_REGISTER(vdst & 0x0f)).obejct, mclass, id_method,
                                reg);
                        break;
                    case 'I':
                        returnval.val.i = env->CallNonvirtualIntMethodA(
                                allobject.at(GET_REGISTER(vdst & 0x0f)).obejct, mclass, id_method,
                                reg);
                        break;
                    case 'J':
                        returnval.val.j = env->CallNonvirtualLongMethodA(
                                allobject.at(GET_REGISTER(vdst & 0x0f)).obejct, mclass, id_method,
                                reg);
                        break;
                    case 'D':
                        returnval.val.d = env->CallNonvirtualDoubleMethodA(
                                allobject.at(GET_REGISTER(vdst & 0x0f)).obejct, mclass, id_method,
                                reg);
                        break;
                    case '[':
                    case 'L':
                        returnval.val.l = env->CallNonvirtualObjectMethodA(
                                allobject.at(GET_REGISTER(vdst & 0x0f)).obejct, mclass, id_method,
                                reg);
                        break;
                    case 'V':
                    case '\0':
                        env->CallNonvirtualVoidMethodA(
                                allobject.at(GET_REGISTER(vdst)).obejct,
                                mclass, id_method, reg);
                        break;
                    default:
                        MY_LOG_WARNING("cannot get the currect type");
                        break;
                }
                break;
            }
            proto1++;
        }


    }
    FINISH(4);
    OP_END

    HANDLE_OPCODE(OP_INVOKE_DIRECT)
    {
        jobject thisPtr;
        vsrc1 = INST_AA(inst);
        orgref = FETCH(1);
        vdst = FETCH(2);
        curref = FETCH(3);
//        assert((vsrc1 >> 4) > 0);
        MY_LOG_VERBOSE("|invoke-direct args=%d @0x%04x {regs=0x%04x %x}", vsrc1 >> 4, orgref, vdst,
                       vsrc1 & 0x0f);
        char *classname = ycFile->mYcFormat.mStringItem[ycFile->mYcFormat.mTypeItem[ycFile->mYcFormat.mMethodItem[curref].classnameIdx]];
        jclass mclass = env->FindClass(classname);
        char *methodname = ycFile->mYcFormat.mStringItem[ycFile->mYcFormat.mMethodItem[curref].methodnameIdx];
        char *proto = ycFile->mYcFormat.mStringItem[ycFile->mYcFormat.mMethodItem[curref].protoIdx];
        char *proto1 = proto;

        jmethodID id_method = env->GetMethodID(mclass, methodname, proto);
        int args = (vsrc1 >> 4) - 1;//参数的个数，参数比寄存器个数少一，因为第一个指代的是this.
        jvalue reg[args];  //参数的类型,
        thisPtr = allobject.at(GET_REGISTER(vdst & 0x0f)).obejct;

        //????????
       /// SET_REGISTER(vdst & 0xf, allobject.size() - 1);
//        } else {
//            jobject getclass1 = mobject.at(GET_REGISTER(vdst & 0x0f));
//            thisPtr = env->NewWeakGlobalRef(getclass1);
//        }
        //这句也要加上
        int counter = 1;
//        char arrayParaProto[5];//定义Proto部分的参数个数及参数类型。
        while (counter <= args && *proto != NULL) {//对Proto中几个参数的类型进行判断并写入
            if (*proto == '(' || *proto == ';' || *proto == 'I' || *proto == 'Z' || *proto == 'B'
                || *proto == 'S' || *proto == 'C' || *proto == 'J' || *proto == 'D') {
                switch (*++proto) {
                    case 'Z':
                        if (counter < 5) {
                            reg[counter - 1].z = GET_REGISTER((vdst >> (counter * 4)) & 0xf);
                        } else {
                            reg[counter - 1].z = GET_REGISTER(vsrc1 & 0xf);
                        }
                        counter++;
                        proto--;
                        break;
                    case 'B':
                        if (counter < 5) {
                            reg[counter - 1].b = GET_REGISTER((vdst >> (counter * 4)) & 0xf);
                        } else {
                            reg[counter - 1].b = GET_REGISTER(vsrc1 & 0xf);
                        }
                        counter++;
                        proto--;
                        break;
                    case 'S':
                        if (counter < 5) {
                            reg[counter - 1].s = GET_REGISTER((vdst >> (counter * 4)) & 0xf);
                        } else {
                            reg[counter - 1].s = GET_REGISTER(vsrc1 & 0xf);
                        }
                        counter++;
                        proto--;
                        break;
                    case 'C':
                        if (counter < 5) {
                            reg[counter - 1].c = GET_REGISTER((vdst >> (counter * 4)) & 0xf);
                        } else {
                            reg[counter - 1].c = GET_REGISTER(vsrc1 & 0xf);
                        }
                        counter++;
                        proto--;
                        break;
                    case 'I':
                        if (counter < 5) {
                            reg[counter - 1].i = GET_REGISTER((vdst >> (counter * 4)) & 0xf);
                        } else {
                            reg[counter - 1].i = GET_REGISTER(vsrc1 & 0xf);
                        }
                        counter++;
                        proto--;
                        break;
                    case 'J':
                        if (counter < 5) {
                            reg[counter - 1].j = GET_REGISTER((vdst >> (counter * 4)) & 0xf);
                        } else {
                            reg[counter - 1].j = GET_REGISTER(vsrc1 & 0xf);
                        }
                        counter++;
                        proto--;
                        break;
                    case 'F':
                        if (counter < 5) {
                            reg[counter - 1].f = GET_REGISTER((vdst >> (counter * 4)) & 0xf);
                        } else {
                            reg[counter - 1].f = GET_REGISTER(vsrc1 & 0xf);
                        }
                        counter++;
                        proto--;
                        break;
                    case 'D':
                        if (counter < 5) {
                            reg[counter - 1].d = GET_REGISTER((vdst >> (counter * 4)) & 0xf);
                        } else {
                            reg[counter - 1].d = GET_REGISTER(vsrc1 & 0xf);
                        }
                        counter++;
                        proto--;
                        break;
                    case '[':
                    case 'L':
                        if (counter < 5) {
                            reg[counter - 1].l = allobject.at(
                                    GET_REGISTER((vdst >> (counter * 4)) & 0xf)).obejct;
                        } else {
                            reg[counter - 1].l = allobject.at(GET_REGISTER(vsrc1 & 0xf)).obejct;
                        }
                        counter++;
                        proto--;
                        break;
                    default:
                        proto--;
                        break;
                }
            }
            proto++;
        }
//        if (args < 5) {//proto参数个数小于5，判别并获取寄存器内容。
//            for (int k = 0; k < args; k++) {
//                reg[k].i = GET_REGISTER((vdst >> (k * 4)) & 0xf);
//            }
//        } else {//proto参数个数等于5，判别并获取寄存器内容。
//            for (int k = 0; k < args - 1; k++) {
//                reg[k].i = GET_REGISTER((vdst >> (k * 4)) & 0xf);
//            }
//            reg[4].i = GET_REGISTER(vsrc1 & 0xf);
//        }
        while (true) {
            if (*proto1 == ')') {
                switch (*++proto1) {
                    case 'Z':
                        env->CallBooleanMethodA(thisPtr, id_method, reg);
                        break;
                    case 'B':
                        env->CallByteMethodA(thisPtr, id_method, reg);
                        break;
                    case 'S':
                        env->CallShortMethodA(thisPtr, id_method, reg);
                        break;
                    case 'C':
                        env->CallCharMethodA(thisPtr, id_method, reg);
                        break;
                    case 'I':
                        env->CallIntMethodA(thisPtr, id_method, reg);
                        break;
                    case 'J':
                        env->CallLongMethodA(thisPtr, id_method, reg);
                        break;
                    case 'F':
                        env->CallFloatMethodA(thisPtr, id_method, reg);
                    case 'D':
                        env->CallDoubleMethodA(thisPtr, id_method, reg);
                        break;
                    case '[':
                    case 'L':
                        env->CallObjectMethodA(thisPtr, id_method, reg);
                        break;
                    case 'V':
                    case '\0':
                        env->CallVoidMethodA(thisPtr, id_method, reg);
                        break;
                    default:
                        MY_LOG_WARNING("cannot get the currect type");
                        break;
                }
                break;
            }
            proto1++;
        }

        //分配一块指定大小的内存给新创建的对象使用。应该是这样的吧。
        //mObject[mObjectsize]=env->NewObject(mClass[GET_REGISTER(vdst)],id_method);
//    MY_LOG_VERBOSE("nihao");
//        mobject.push_back(env->NewObject((jclass) mobject.at(GET_REGISTER(vdst)), id_method));
//    MY_LOG_VERBOSE("nih");
        //mObject=(jobject*)realloc(mObject, sizeof(jobject)*(mObjectsize+1));
//        SET_REGISTER(vdst, mobject.size() - 1);
        // SET_REGISTER(vdst,mObjectsize++);
    }
    FINISH(4);
    OP_END


    HANDLE_OPCODE(OP_INVOKE_STATIC)
    {
        jobject *thisPtr;
        vsrc1 = INST_AA(inst);
        orgref = FETCH(1);
        vdst = FETCH(2);
        curref = FETCH(3);
//        assert((vsrc1 >> 4) > 0);
        MY_LOG_VERBOSE("|invoke-static args=%d @0x%04x {regs=0x%04x %x}",
                       vsrc1 >> 4, orgref, vdst, vsrc1 & 0x0f);
        char *classname = ycFile->mYcFormat.mStringItem[ycFile->mYcFormat.mTypeItem[ycFile->mYcFormat.mMethodItem[curref].classnameIdx]];
        jclass mclass = env->FindClass(classname);
        char *methodname = ycFile->mYcFormat.mStringItem[ycFile->mYcFormat.mMethodItem[curref].methodnameIdx];
        char *proto = ycFile->mYcFormat.mStringItem[ycFile->mYcFormat.mMethodItem[curref].protoIdx];
        char *proto1 = proto;//CallStaticBooleanMethodA需要对类型进行判断。
        jmethodID id_method = env->GetStaticMethodID(mclass, methodname, proto);
        int args = (vsrc1 >> 4);//参数的个数，获取寄存器长度。
        jvalue reg[args];  //参数的类型,
        int counter = 0;
        int testnumber11;
//        char arrayParaProto[5];//定义Proto部分的参数个数及参数类型。
        while (counter < args && *proto != NULL) {//对Proto中几个参数的类型进行判断并写入
            if (*proto == '(' || *proto == ';' || *proto == 'I' || *proto == 'Z' || *proto == 'B'
                || *proto == 'S' || *proto == 'C' || *proto == 'J' || *proto == 'D') {
                switch (*++proto) {
                    case 'Z':
                        if (counter < 5) {
                            reg[counter].z = GET_REGISTER((vdst >> (counter * 4)) & 0xf);
                        } else {
                            reg[counter].z = GET_REGISTER(vsrc1 & 0xf);
                        }
                        counter++;
                        proto--;
                        break;
                    case 'B':
                        if (counter < 5) {
                            reg[counter].b = GET_REGISTER((vdst >> (counter * 4)) & 0xf);
                        } else {
                            reg[counter].b = GET_REGISTER(vsrc1 & 0xf);
                        }
                        counter++;
                        proto--;
                        break;
                    case 'S':
                        if (counter < 5) {
                            reg[counter].s = GET_REGISTER((vdst >> (counter * 4)) & 0xf);
                        } else {
                            reg[counter].s = GET_REGISTER(vsrc1 & 0xf);
                        }
                        counter++;
                        proto--;
                        break;
                    case 'C':
                        if (counter < 5) {
                            reg[counter].c = GET_REGISTER((vdst >> (counter * 4)) & 0xf);
                        } else {
                            reg[counter].c = GET_REGISTER(vsrc1 & 0xf);
                        }
                        counter++;
                        proto--;
                        break;
                    case 'I':
                        testnumber11 = (vdst >> (counter * 4)) & 0xf;
                        if (counter < 5) {
                            reg[counter].i = GET_REGISTER((vdst >> (counter * 4)) & 0xf);
                        } else {
                            reg[counter].i = GET_REGISTER(vsrc1 & 0xf);
                        }
                        counter++;
                        proto--;
                        break;
                    case 'J':
                        if (counter < 5) {
                            reg[counter].j = GET_REGISTER((vdst >> (counter * 4)) & 0xf);
                        } else {
                            reg[counter].j = GET_REGISTER(vsrc1 & 0xf);
                        }
                        counter++;
                        proto--;
                        break;
                    case 'F':
                        if (counter < 5) {
                            reg[counter].f = GET_REGISTER((vdst >> (counter * 4)) & 0xf);
                        } else {
                            reg[counter].f = GET_REGISTER(vsrc1 & 0xf);
                        }
                        counter++;
                        proto--;
                        break;
                    case 'D':
                        if (counter < 5) {
                            reg[counter].d = GET_REGISTER((vdst >> (counter * 4)) & 0xf);
                        } else {
                            reg[counter].d = GET_REGISTER(vsrc1 & 0xf);
                        }
                        counter++;
                        proto--;
                        break;
                    case '[':
                    case 'L':
                        testnumber11 = (vdst >> (counter * 4)) & 0xf;
                        if (counter < 5) {
//                            reg[counter].l = mobject.at(
//                                    GET_REGISTER((vdst >> (counter * 4)) & 0xf));
                            reg[counter].l = allobject.at(
                                    GET_REGISTER((vdst >> (counter * 4)) & 0xf)).obejct;
                        } else {
                            reg[counter].l = allobject.at(GET_REGISTER(vsrc1 & 0xf)).obejct;
                        }
                        counter++;
                        proto--;
                        break;
                    default:
                        proto--;
                        break;
                }
            }
            proto++;
        }
        //while(*(proto--)==')'
        while (true) {
            if (*proto1 == ')') {
                switch (*++proto1) {
                    case 'Z':
                        returnval.val.z = env->CallStaticBooleanMethodA(mclass, id_method, reg);
                        break;
                    case 'B':
                        returnval.val.b = env->CallStaticByteMethodA(mclass, id_method, reg);
                        break;
                    case 'S':
                        returnval.val.s = env->CallStaticShortMethodA(mclass, id_method, reg);
                        break;
                    case 'C':
                        returnval.val.c = env->CallStaticCharMethodA(mclass, id_method, reg);
                        break;
                    case 'I':
                        returnval.val.i = env->CallStaticIntMethodA(mclass, id_method, reg);
                        break;
                    case 'J':
                        returnval.val.j = env->CallStaticLongMethodA(mclass, id_method, reg);
                        break;
                    case 'F':
                        returnval.val.f = env->CallStaticFloatMethodA(mclass, id_method, reg);
                    case 'D':
                        returnval.val.d = env->CallStaticDoubleMethodA(mclass, id_method, reg);
                        break;
                    case '[':
                    case 'L':
                        returnval.val.l = env->CallStaticObjectMethodA(mclass, id_method, reg);
                        break;
                    case 'V':
                    case '\0':
                        env->CallStaticVoidMethodA(mclass, id_method, reg);
                        break;
                    default:
                        MY_LOG_WARNING("cannot get the currect type");
                        break;
                }
                break;
            }
            proto1++;
        }
    }
    FINISH(4);
    OP_END

    HANDLE_OPCODE(OP_INVOKE_INTERFACE)
    {
        jobject thisPtr;
        vsrc1 = INST_AA(inst);
        orgref = FETCH(1);
        vdst = FETCH(2);
        curref = FETCH(3);
//        assert((vsrc1 >> 4) > 0);
        MY_LOG_VERBOSE("|invoke-interface args=%d @0x%04x {regs=0x%04x %x}",
                       vsrc1 >> 4, orgref, vdst, vsrc1 & 0x0f);
        char *classname = ycFile->mYcFormat.mStringItem[ycFile->mYcFormat.mTypeItem[ycFile->mYcFormat.mMethodItem[curref].classnameIdx]];
        //获取类名
        jclass mclass = env->FindClass(classname);
        //获取指定对象的类在java中定义
        char *methodname = ycFile->mYcFormat.mStringItem[ycFile->mYcFormat.mMethodItem[curref].methodnameIdx];
        //获取方法名
        char *proto = ycFile->mYcFormat.mStringItem[ycFile->mYcFormat.mMethodItem[curref].protoIdx];
        //获取参数
        char *proto1 = proto;
        jmethodID id_method = env->GetMethodID(mclass, methodname, proto);
        //通过类名、方法名、参数类型获取方法。id_method.
        int args = (vsrc1 >> 4) - 1;
        int counter = 1;
        //proto中参数的多少，第一个是this调用自身的，故而少一。
        jvalue reg[args];
        //proto类型及内容的存储。
//        jclass getclass1 = (jclass) mobject.at(GET_REGISTER(vdst & 0x0f));
//        thisPtr = env->NewObject(testclass, id_method);
//        //调用NewObject方法创建java.util.Date对象
//        jobject  jobject1 = mobject.at(GET_REGISTER(vdst & 0x0f));
//        env->CallVoidMethodA(thisPtr, id_method, reg);
//        env->CallVoidMethod(jclass1, id_method);

        while (counter <= args && *proto != NULL) {//对Proto中几个参数的类型进行判断并写入
            if (*proto == '(' || *proto == ';' || *proto == 'I' || *proto == 'Z' || *proto == 'B'
                || *proto == 'S' || *proto == 'C' || *proto == 'J' || *proto == 'D') {
                switch (*++proto) {
                    case 'Z':
                        if (counter < 5) {
                            reg[counter - 1].z = GET_REGISTER((vdst >> (counter * 4)) & 0xf);
                        } else {
                            reg[counter - 1].z = GET_REGISTER(vsrc1 & 0xf);
                        }
                        counter++;
                        proto--;
                        break;
                    case 'B':
                        if (counter < 5) {
                            reg[counter - 1].b = GET_REGISTER((vdst >> (counter * 4)) & 0xf);
                        } else {
                            reg[counter - 1].b = GET_REGISTER(vsrc1 & 0xf);
                        }
                        counter++;
                        proto--;
                        break;
                    case 'S':
                        if (counter < 5) {
                            reg[counter - 1].s = GET_REGISTER((vdst >> (counter * 4)) & 0xf);
                        } else {
                            reg[counter - 1].s = GET_REGISTER(vsrc1 & 0xf);
                        }
                        counter++;
                        proto--;
                        break;
                    case 'C':
                        if (counter < 5) {
                            reg[counter - 1].c = GET_REGISTER((vdst >> (counter * 4)) & 0xf);
                        } else {
                            reg[counter - 1].c = GET_REGISTER(vsrc1 & 0xf);
                        }
                        counter++;
                        proto--;
                        break;
                    case 'I':
                        if (counter < 5) {
                            reg[counter - 1].i = GET_REGISTER((vdst >> (counter * 4)) & 0xf);
                        } else {
                            reg[counter - 1].i = GET_REGISTER(vsrc1 & 0xf);
                        }
                        counter++;
                        proto--;
                        break;
                    case 'J':
                        if (counter < 5) {
                            reg[counter - 1].j = GET_REGISTER((vdst >> (counter * 4)) & 0xf);
                        } else {
                            reg[counter - 1].j = GET_REGISTER(vsrc1 & 0xf);
                        }
                        counter++;
                        proto--;
                        break;
                    case 'F':
                        if (counter < 5) {
                            reg[counter - 1].f = GET_REGISTER((vdst >> (counter * 4)) & 0xf);
                        } else {
                            reg[counter - 1].f = GET_REGISTER(vsrc1 & 0xf);
                        }
                        counter++;
                        proto--;
                        break;
                    case 'D':
                        if (counter < 5) {
                            reg[counter - 1].d = GET_REGISTER((vdst >> (counter * 4)) & 0xf);
                        } else {
                            reg[counter - 1].d = GET_REGISTER(vsrc1 & 0xf);
                        }
                        counter++;
                        proto--;
                        break;
                    case '[':
                    case 'L':
                        if (counter < 5) {
                            reg[counter - 1].l = allobject.at(
                                    GET_REGISTER((vdst >> (counter * 4)) & 0xf)).obejct;
                        } else {
                            reg[counter - 1].l = allobject.at(GET_REGISTER(vsrc1 & 0xf)).obejct;
                        }
                        counter++;
                        proto--;
                        break;
                    default:
                        proto--;
                        break;
                }
            }
            proto++;
        }

        while (true) {
            if (*proto1 == ')') {
                switch (*++proto1) {
                    case 'Z':
                        returnval.val.z = env->CallBooleanMethodA(
                                allobject.at(GET_REGISTER(vdst & 0x0f)).obejct,
                                id_method, reg);
                        break;
                    case 'B':
                        returnval.val.b = env->CallByteMethodA(
                                allobject.at(GET_REGISTER(vdst & 0x0f)).obejct,
                                id_method, reg);
                        break;
                    case 'S':
                        returnval.val.s = env->CallShortMethodA(
                                allobject.at(GET_REGISTER(vdst & 0x0f)).obejct,
                                id_method, reg);
                        break;
                    case 'C':
                        returnval.val.c = env->CallCharMethodA(
                                allobject.at(GET_REGISTER(vdst & 0x0f)).obejct,
                                id_method, reg);
                        break;
                    case 'I':
                        returnval.val.i = env->CallIntMethodA(
                                allobject.at(GET_REGISTER(vdst & 0x0f)).obejct,
                                id_method, reg);
                        break;
                    case 'J':
                        returnval.val.j = env->CallLongMethodA(
                                allobject.at(GET_REGISTER(vdst & 0x0f)).obejct,
                                id_method, reg);
                        break;
                    case 'D':
                        returnval.val.d = env->CallDoubleMethodA(
                                allobject.at(GET_REGISTER(vdst & 0x0f)).obejct,
                                id_method, reg);
                        break;
                    case '[':
                    case 'L':
                        returnval.val.l = env->CallObjectMethodA(
                                allobject.at(GET_REGISTER(vdst & 0x0f)).obejct,
                                id_method, reg);
                        break;
                    case 'V':
                    case '\0':
                        env->CallVoidMethodA(
                                allobject.at(GET_REGISTER(vdst & 0x0f)).obejct,
                                id_method, reg);
                        break;
                    default:
                        MY_LOG_WARNING("cannot get the currect type");
                        break;
                }
                break;
            }
            proto1++;
        }
        //retval.i=env->CallIntMethodA(mObject[GET_REGISTER(vdst & 0x0f)],id_method,reg);
    }
    FINISH(4);
    OP_END;
    HANDLE_OPCODE(OP_UNUSED_73)
    OP_END
    HANDLE_OPCODE(OP_INVOKE_VIRTUAL_RANGE)
    {
        jobject thisPtr;
        vsrc1 = INST_AA(inst);
        orgref = FETCH(1);
        vdst = FETCH(2);
        curref = FETCH(3);
//        assert((vsrc1 >> 4) > 0);
        MY_LOG_VERBOSE("|invoke-virtual/range args=%d @0x%04x {regs=0x%04x %x}",
                       vsrc1 >> 4, orgref, vdst, vsrc1 & 0x0f);
        char *classname = ycFile->mYcFormat.mStringItem[ycFile->mYcFormat.mTypeItem[ycFile->mYcFormat.mMethodItem[curref].classnameIdx]];
        jclass mclass = env->FindClass(classname);
        char *methodname = ycFile->mYcFormat.mStringItem[ycFile->mYcFormat.mMethodItem[curref].methodnameIdx];
        char *proto = ycFile->mYcFormat.mStringItem[ycFile->mYcFormat.mMethodItem[curref].protoIdx];
        char *proto1 = proto;
//        char *type111 = ycFile->mYcFormat.mStringItem[curref];
        jmethodID id_method = env->GetMethodID(mclass, methodname, proto);
        int args = vsrc1 - 1;


        jvalue reg[args];
        int test1 =vdst&0xff;
        int counter = 1;
//        env->CallVoidMethod(allobject.at(GET_REGISTER(vdst & 0xff)).obejct,id_method);

        while (counter <= args && *proto != NULL) {//对Proto中几个参数的类型进行判断并写入
            if (*proto == '(' || *proto == ';' || *proto == 'I' || *proto == 'Z' || *proto == 'B'
                || *proto == 'S' || *proto == 'C' || *proto == 'J' || *proto == 'D') {
                switch (*++proto) {
                    case 'Z':
                        reg[counter-1].z = GET_REGISTER(vdst + counter);
                        counter++;
                        proto--;
                        break;
                    case 'B':
                        reg[counter-1].b = GET_REGISTER(vdst + counter);
                        counter++;
                        proto--;
                        break;
                    case 'S':
                        reg[counter-1].s = GET_REGISTER(vdst + counter);
                        counter++;
                        proto--;
                        break;
                    case 'C':
                        reg[counter-1].c = GET_REGISTER(vdst + counter);
                        counter++;
                        proto--;
                        break;
                    case 'I':
                        reg[counter-1].i = GET_REGISTER(vdst + counter);
                        counter++;
                        proto--;
                        break;
                    case 'J':
                        reg[counter-1].j = GET_REGISTER(vdst + counter);
                        counter++;
                        proto--;
                        break;
                    case 'F':
                        reg[counter-1].f = GET_REGISTER(vdst + counter);
                        counter++;
                        proto--;
                        break;
                    case 'D':
                        reg[counter-1].d = GET_REGISTER(vdst + counter);
                        counter++;
                        proto--;
                        break;
                    case '[':
                    case 'L':
                        reg[counter-1].l = allobject.at(GET_REGISTER(vdst + counter)).obejct;
                        counter++;
                        proto--;
                        break;
                    default:
                        proto--;
                        break;
                }
            }
            proto++;
        }

//while(*(proto--)==')'
        while (true) {
            if (*proto1 == ')') {
                switch (*++proto1) {
                    case 'Z':
                        returnval.val.z = env->CallBooleanMethodA(
                                allobject.at(GET_REGISTER(vdst)).obejct,
                                id_method, reg);
                        break;
                    case 'B':
                        returnval.val.b = env->CallByteMethodA(
                                allobject.at(GET_REGISTER(vdst)).obejct,
                                id_method, reg);
                        break;
                    case 'S':
                        returnval.val.s = env->CallShortMethodA(
                                allobject.at(GET_REGISTER(vdst)).obejct,
                                id_method, reg);
                        break;
                    case 'C':
                        returnval.val.c = env->CallCharMethodA(
                                allobject.at(GET_REGISTER(vdst)).obejct,
                                id_method, reg);
                        break;
                    case 'I':
                        returnval.val.i = env->CallIntMethodA(
                                allobject.at(GET_REGISTER(vdst)).obejct,
                                id_method, reg);
                        break;
                    case 'J':
                        returnval.val.j = env->CallLongMethodA(
                                allobject.at(GET_REGISTER(vdst)).obejct,
                                id_method, reg);
                        break;
                    case 'D':
                        returnval.val.d = env->CallDoubleMethodA(
                                allobject.at(GET_REGISTER(vdst)).obejct,
                                id_method, reg);
                        break;
                    case '[':
                    case 'L':
                        returnval.val.l = env->CallObjectMethodA(
                                allobject.at(GET_REGISTER(vdst)).obejct,
                                id_method, reg);
                        break;
                    case 'V':
                    case '\0':
                        env->CallVoidMethodA(allobject.at(GET_REGISTER(vdst)).obejct,
                                             id_method, reg);
                        break;
                    default:
                        MY_LOG_WARNING("cannot get the currect type");
                        break;
                }
                break;
            }
            proto1++;
        }
        //retval.i=env->CallIntMethodA(mObject[GET_REGISTER(vdst & 0x0f)],id_method,reg);
    }
    FINISH(4);
    OP_END
    HANDLE_OPCODE(OP_INVOKE_SUPER_RANGE)
    {
        jobject *thisPtr;
        vsrc1 = INST_AA(inst);
        orgref = FETCH(1);
        vdst = FETCH(2);
        curref = FETCH(3);
//        assert((vsrc1 >> 4) > 0);
        MY_LOG_VERBOSE("|invoke-super/range args=%d @0x%04x {regs=0x%04x %x}",
                       vsrc1 >> 4, orgref, vdst, vsrc1 & 0x0f);
        //��������
        char *classname = ycFile->mYcFormat.mStringItem[ycFile->mYcFormat.mTypeItem[ycFile->mYcFormat.mMethodItem[curref].classnameIdx]];
        jclass mclass = env->FindClass(classname);
        char *methodname = ycFile->mYcFormat.mStringItem[ycFile->mYcFormat.mMethodItem[curref].methodnameIdx];
        char *proto = ycFile->mYcFormat.mStringItem[ycFile->mYcFormat.mMethodItem[curref].protoIdx];
        char *proto1 = proto;
        jmethodID id_method = env->GetMethodID(mclass, methodname, proto);
        int args = vsrc1-1;
        jvalue reg[args];
        int counter = 1;
        while (counter <= args && *proto != NULL) {//对Proto中几个参数的类型进行判断并写入
            if (*proto == '(' || *proto == ';' || *proto == 'I' || *proto == 'Z' || *proto == 'B'
                || *proto == 'S' || *proto == 'C' || *proto == 'J' || *proto == 'D') {
                switch (*++proto) {
                    case 'Z':
                        reg[counter-1].z = GET_REGISTER(vdst + counter);
                        counter++;
                        proto--;
                        break;
                    case 'B':
                        reg[counter-1].b = GET_REGISTER(vdst + counter);
                        counter++;
                        proto--;
                        break;
                    case 'S':
                        reg[counter-1].s = GET_REGISTER(vdst + counter);
                        counter++;
                        proto--;
                        break;
                    case 'C':
                        reg[counter-1].c = GET_REGISTER(vdst + counter);
                        counter++;
                        proto--;
                        break;
                    case 'I':
                        reg[counter-1].i = GET_REGISTER(vdst + counter);
                        counter++;
                        proto--;
                        break;
                    case 'J':
                        reg[counter-1].j = GET_REGISTER(vdst + counter);
                        counter++;
                        proto--;
                        break;
                    case 'F':
                        reg[counter-1].f = GET_REGISTER(vdst + counter);
                        counter++;
                        proto--;
                        break;
                    case 'D':
                        reg[counter-1].d = GET_REGISTER(vdst + counter);
                        counter++;
                        proto--;
                        break;
                    case '[':
                    case 'L':
                        reg[counter-1].l = allobject.at(GET_REGISTER(vdst + counter)).obejct;
                        counter++;
                        proto--;
                        break;
                    default:
                        proto--;
                        break;
                }
            }
            proto++;
        }
        //env->CallNonvirtualVoidMethodA(mObject[GET_REGISTER(vdst&0x0f)],mclass,id_method,reg);
        //while(*(proto--)==')'
        while (true) {
            if (*proto1 == ')') {
                switch (*++proto1) {
                    case 'Z':
                        returnval.val.z = env->CallNonvirtualBooleanMethodA(
                                allobject.at(GET_REGISTER(vdst)).obejct, mclass, id_method,
                                reg);
                        break;
                    case 'B':
                        returnval.val.b = env->CallNonvirtualByteMethodA(
                                allobject.at(GET_REGISTER(vdst)).obejct, mclass, id_method,
                                reg);
                        break;
                    case 'S':
                        returnval.val.s = env->CallNonvirtualShortMethodA(
                                allobject.at(GET_REGISTER(vdst)).obejct, mclass, id_method,
                                reg);
                        break;
                    case 'C':
                        returnval.val.c = env->CallNonvirtualCharMethodA(
                                allobject.at(GET_REGISTER(vdst)).obejct, mclass, id_method,
                                reg);
                        break;
                    case 'I':
                        returnval.val.i = env->CallNonvirtualIntMethodA(
                                allobject.at(GET_REGISTER(vdst)).obejct, mclass, id_method,
                                reg);
                        break;
                    case 'J':
                        returnval.val.j = env->CallNonvirtualLongMethodA(
                                allobject.at(GET_REGISTER(vdst)).obejct, mclass, id_method,
                                reg);
                        break;
                    case 'D':
                        returnval.val.d = env->CallNonvirtualDoubleMethodA(
                                allobject.at(GET_REGISTER(vdst)).obejct, mclass, id_method,
                                reg);
                        break;
                    case '[':
                    case 'L':
                        returnval.val.l = env->CallNonvirtualObjectMethodA(
                                allobject.at(GET_REGISTER(vdst)).obejct, mclass, id_method,
                                reg);
                        break;
                    case '\0':
                    case 'V':
                        env->CallNonvirtualVoidMethodA(
                                allobject.at(GET_REGISTER(vdst)).obejct,
                                mclass, id_method, reg);
                        break;
                    default:
                        MY_LOG_WARNING("cannot get the currect type");
                        break;
                }
                break;
            }
            proto1++;
        }


    }
    FINISH(4);
    OP_END
    HANDLE_OPCODE(OP_INVOKE_DIRECT_RANGE)
    {
        //��������
        jobject thisPtr;
        vsrc1 = INST_AA(inst);
        orgref = FETCH(1);
        vdst = FETCH(2);
        curref = FETCH(3);
//        assert((vsrc1 >> 4) > 0);
        MY_LOG_VERBOSE("|invoke-direct-range args=%d @0x%04x {regs=0x%04x %x}", vsrc1 >> 4, orgref, vdst,
                       vsrc1 & 0x0f);
        char *classname = ycFile->mYcFormat.mStringItem[ycFile->mYcFormat.mTypeItem[ycFile->mYcFormat.mMethodItem[curref].classnameIdx]];
        jclass mclass = env->FindClass(classname);
        char *methodname = ycFile->mYcFormat.mStringItem[ycFile->mYcFormat.mMethodItem[curref].methodnameIdx];
        char *proto = ycFile->mYcFormat.mStringItem[ycFile->mYcFormat.mMethodItem[curref].protoIdx];
        char *proto1 = proto;

        jmethodID id_method = env->GetMethodID(mclass, methodname, proto);
        int args = vsrc1 -1 ;
        jvalue reg[args];  //参数的类型,
        thisPtr = allobject.at(GET_REGISTER(vdst)).obejct;

        //????????
        int counter = 1;
        while (counter <= args && *proto != NULL) {//对Proto中几个参数的类型进行判断并写入
            if (*proto == '(' || *proto == ';' || *proto == 'I' || *proto == 'Z' || *proto == 'B'
                || *proto == 'S' || *proto == 'C' || *proto == 'J' || *proto == 'D') {
                switch (*++proto) {
                    case 'Z':
                        reg[counter-1].z = GET_REGISTER(vdst + counter);
                        counter++;
                        proto--;
                        break;
                    case 'B':
                        reg[counter-1].b = GET_REGISTER(vdst + counter);
                        counter++;
                        proto--;
                        break;
                    case 'S':
                        reg[counter].s = GET_REGISTER(vdst + counter);
                        counter++;
                        proto--;
                        break;
                    case 'C':
                        reg[counter-1].c = GET_REGISTER(vdst + counter);
                        counter++;
                        proto--;
                        break;
                    case 'I':
                        reg[counter-1].i = GET_REGISTER(vdst + counter);
                        counter++;
                        proto--;
                        break;
                    case 'J':
                        reg[counter-1].j = GET_REGISTER(vdst + counter);
                        counter++;
                        proto--;
                        break;
                    case 'F':
                        reg[counter-1].f = GET_REGISTER(vdst + counter);
                        counter++;
                        proto--;
                        break;
                    case 'D':
                        reg[counter-1].d = GET_REGISTER(vdst + counter);
                        counter++;
                        proto--;
                        break;
                    case '[':
                    case 'L':
                        reg[counter-1].l = allobject.at(GET_REGISTER(vdst + counter)).obejct;
                        counter++;
                        proto--;
                        break;
                    default:
                        proto--;
                        break;
                }
            }
            proto++;
        }

        while (true) {
            if (*proto1 == ')') {
                switch (*++proto1) {
                    case 'Z':
                        env->CallBooleanMethodA(thisPtr, id_method, reg);
                        break;
                    case 'B':
                        env->CallByteMethodA(thisPtr, id_method, reg);
                        break;
                    case 'S':
                        env->CallShortMethodA(thisPtr, id_method, reg);
                        break;
                    case 'C':
                        env->CallCharMethodA(thisPtr, id_method, reg);
                        break;
                    case 'I':
                        env->CallIntMethodA(thisPtr, id_method, reg);
                        break;
                    case 'J':
                        env->CallLongMethodA(thisPtr, id_method, reg);
                        break;
                    case 'F':
                        env->CallFloatMethodA(thisPtr, id_method, reg);
                    case 'D':
                        env->CallDoubleMethodA(thisPtr, id_method, reg);
                        break;
                    case '[':
                    case 'L':
                        env->CallObjectMethodA(thisPtr, id_method, reg);
                        break;
                    case 'V':
                    case '\0':
                        env->CallVoidMethodA(thisPtr, id_method, reg);
                        break;
                    default:
                        MY_LOG_WARNING("cannot get the currect type");
                        break;
                }
                break;
            }
            proto1++;
        }
    }
    FINISH(4);
    OP_END
//        jmethodID id_method = env->GetMethodID((jclass) mobject.at(GET_REGISTER(thisReg)),
//                                               methodname,
//                                               proto);
//mObject[mObjectsize]=env->NewObject(mClass[GET_REGISTER(vdst)],id_method);
//    MY_LOG_VERBOSE("nihao");
//        mobject.push_back(env->NewObject((jclass) mobject.at(GET_REGISTER(thisReg)), id_method));
//    MY_LOG_VERBOSE("nih");
//mObject=(jobject*)realloc(mObject, sizeof(jobject)*(mObjectsize+1));
//        SET_REGISTER(vdst, mobject.size() - 1);
// SET_REGISTER(vdst,mObjectsize++);


    HANDLE_OPCODE(OP_INVOKE_STATIC_RANGE)
    {
        jobject *thisPtr;
        vsrc1 = INST_AA(inst);
        orgref = FETCH(1);
        vdst = FETCH(2);
        curref = FETCH(3);
//    assert((vsrc1>>4) > 0);
        MY_LOG_VERBOSE("|invoke-static/range args=%d @0x%04x {regs=0x%04x %x}",
                       vsrc1 >> 4, orgref, vdst, vsrc1 & 0x0f);
        char *classname = ycFile->mYcFormat.mStringItem[ycFile->mYcFormat.mTypeItem[ycFile->mYcFormat.mMethodItem[curref].classnameIdx]];
        jclass mclass = env->FindClass(classname);
        char *methodname = ycFile->mYcFormat.mStringItem[ycFile->mYcFormat.mMethodItem[curref].methodnameIdx];
        char *proto = ycFile->mYcFormat.mStringItem[ycFile->mYcFormat.mMethodItem[curref].protoIdx];
        char *proto1 = proto;
        jmethodID id_method = env->GetStaticMethodID(mclass, methodname, proto);
        int args = vsrc1;
        jvalue reg[args];
        int counter = 0;
        char arrayParaProto[5];//定义Proto部分的参数个数及参数类型。
        while (counter < args && *proto != NULL) {//对Proto中几个参数的类型进行判断并写入
            if (*proto == '(' || *proto == ';' || *proto == 'I' || *proto == 'Z' || *proto == 'B'
                || *proto == 'S' || *proto == 'C' || *proto == 'J' || *proto == 'D') {
                switch (*++proto) {
                    case 'Z':
                        reg[counter].z = GET_REGISTER(vdst + counter);
                        counter++;
                        proto--;
                        break;
                    case 'B':
                        reg[counter].b = GET_REGISTER(vdst + counter);
                        counter++;
                        proto--;
                        break;
                    case 'S':
                        reg[counter].s = GET_REGISTER(vdst + counter);
                        counter++;
                        proto--;
                        break;
                    case 'C':
                        reg[counter].c = GET_REGISTER(vdst + counter);
                        counter++;
                        proto--;
                        break;
                    case 'I':
                        reg[counter].i = GET_REGISTER(vdst + counter);
                        counter++;
                        proto--;
                        break;
                    case 'J':
                        reg[counter].j = GET_REGISTER(vdst + counter);
                        counter++;
                        proto--;
                        break;
                    case 'F':
                        reg[counter].f = GET_REGISTER(vdst + counter);
                        counter++;
                        proto--;
                        break;
                    case 'D':
                        reg[counter].d = GET_REGISTER(vdst + counter);
                        counter++;
                        proto--;
                        break;
                    case '[':
                    case 'L':
                        reg[counter].l = allobject.at(GET_REGISTER(vdst + counter)).obejct;
                        counter++;
                        proto--;
                        break;
                    default:
                        proto--;
                        break;
                }
            }
            proto++;
        }
//        int temp = 1;
//            for (int k = 0; k < args; k++) {
//                    int jj = reg[k].i = GET_REGISTER(vdst +k);
//            }
        //while(*(proto--)==')'
        while (true) {
            if (*proto1 == ')') {
                switch (*++proto1) {
                    case 'Z':
                        returnval.val.z = env->CallStaticBooleanMethodA(mclass, id_method, reg);
                        break;
                    case 'B':
                        returnval.val.b = env->CallStaticByteMethodA(mclass, id_method, reg);
                        break;
                    case 'S':
                        returnval.val.s = env->CallStaticShortMethodA(mclass, id_method, reg);
                        break;
                    case 'C':
                        returnval.val.c = env->CallStaticCharMethodA(mclass, id_method, reg);
                        break;
                    case 'I':
                        returnval.val.i = env->CallStaticIntMethodA(mclass, id_method, reg);
                        break;
                    case 'J':
                        returnval.val.j = env->CallStaticLongMethodA(mclass, id_method, reg);
                        break;
                    case 'D':
                        returnval.val.d = env->CallStaticDoubleMethodA(mclass, id_method, reg);
                        break;
                    case '[':
                    case 'L':
                        returnval.val.l = env->CallStaticObjectMethodA(mclass, id_method, reg);
                        break;
                    case 'V':
                    case '\0':
                        env->CallStaticVoidMethodA(mclass, id_method, reg);
                        break;
                    default:
                        MY_LOG_WARNING("cannot get the currect type");
                        break;
                }
                break;
            }
            proto1++;
        }
        //retval.i=env->CallIntMethodA(mObject[GET_REGISTER(vdst & 0x0f)],id_method,reg);
    }
    FINISH(4);
    OP_END

    HANDLE_OPCODE(OP_INVOKE_INTERFACE_RANGE)
    {
        jobject *thisPtr;
        vsrc1 = INST_AA(inst);
        orgref = FETCH(1);
        vdst = FETCH(2);
        curref = FETCH(3);
//        assert((vsrc1 >> 4) > 0);
        MY_LOG_VERBOSE("|invoke-interface/range args=%d @0x%04x {regs=0x%04x %x}",
                       vsrc1 >> 4, orgref, vdst, vsrc1 & 0x0f);
        char *classname = ycFile->mYcFormat.mStringItem[ycFile->mYcFormat.mTypeItem[ycFile->mYcFormat.mMethodItem[curref].classnameIdx]];
        jclass mclass = env->FindClass(classname);
        char *methodname = ycFile->mYcFormat.mStringItem[ycFile->mYcFormat.mMethodItem[curref].methodnameIdx];
        char *proto = ycFile->mYcFormat.mStringItem[ycFile->mYcFormat.mMethodItem[curref].protoIdx];
        char *proto1 = proto;
        jmethodID id_method = env->GetMethodID(mclass, methodname, proto);
        int args = vsrc1-1;
        jvalue reg[args];

        int counter = 1;
        while (counter <= args && *proto != NULL) {//对Proto中几个参数的类型进行判断并写入
            if (*proto == '(' || *proto == ';' || *proto == 'I' || *proto == 'Z' || *proto == 'B'
                || *proto == 'S' || *proto == 'C' || *proto == 'J' || *proto == 'D') {
                switch (*++proto) {
                    case 'Z':
                        reg[counter-1].z = GET_REGISTER(vdst + counter);
                        counter++;
                        proto--;
                        break;
                    case 'B':
                        reg[counter-1].b = GET_REGISTER(vdst + counter);
                        counter++;
                        proto--;
                        break;
                    case 'S':
                        reg[counter-1].s = GET_REGISTER(vdst + counter);
                        counter++;
                        proto--;
                        break;
                    case 'C':
                        reg[counter-1].c = GET_REGISTER(vdst + counter);
                        counter++;
                        proto--;
                        break;
                    case 'I':
                        reg[counter-1].i = GET_REGISTER(vdst + counter);
                        counter++;
                        proto--;
                        break;
                    case 'J':
                        reg[counter-1].j = GET_REGISTER(vdst + counter);
                        counter++;
                        proto--;
                        break;
                    case 'F':
                        reg[counter-1].f = GET_REGISTER(vdst + counter);
                        counter++;
                        proto--;
                        break;
                    case 'D':
                        reg[counter-1].d = GET_REGISTER(vdst + counter);
                        counter++;
                        proto--;
                        break;
                    case '[':
                    case 'L':
                        reg[counter-1].l = allobject.at(GET_REGISTER(vdst + counter)).obejct;
                        counter++;
                        proto--;
                        break;
                    default:
                        proto--;
                        break;
                }
            }
            proto++;
        }
        //while(*(proto--)==')'
        while (true) {
            if (*proto1 == ')') {
                switch (*++proto1) {
                    case 'Z':
                        returnval.val.z = env->CallBooleanMethodA(
                                allobject.at(GET_REGISTER(vdst)).obejct,
                                id_method, reg);
                        break;
                    case 'B':
                        returnval.val.b = env->CallByteMethodA(
                                allobject.at(GET_REGISTER(vdst)).obejct,
                                id_method, reg);
                        break;
                    case 'S':
                        returnval.val.s = env->CallShortMethodA(
                                allobject.at(GET_REGISTER(vdst)).obejct,
                                id_method, reg);
                        break;
                    case 'C':
                        returnval.val.c = env->CallCharMethodA(
                                allobject.at(GET_REGISTER(vdst)).obejct,
                                id_method, reg);
                        break;
                    case 'I':
                        returnval.val.i = env->CallIntMethodA(
                                allobject.at(GET_REGISTER(vdst)).obejct,
                                id_method, reg);
                        break;
                    case 'J':
                        returnval.val.j = env->CallLongMethodA(
                                allobject.at(GET_REGISTER(vdst)).obejct,
                                id_method, reg);
                        break;
                    case 'D':
                        returnval.val.d = env->CallDoubleMethodA(
                                allobject.at(GET_REGISTER(vdst)).obejct,
                                id_method, reg);
                        break;
                    case '[':
                    case 'L':
                        returnval.val.l = env->CallObjectMethodA(
                                allobject.at(GET_REGISTER(vdst)).obejct,
                                id_method, reg);
                        break;
                    case 'V':
                    case '\0':
                        env->CallVoidMethodA(allobject.at(GET_REGISTER(vdst)).obejct,
                                             id_method, reg);
                        break;
                    default:
                        MY_LOG_WARNING("cannot get the currect type");
                        break;
                }
                break;
            }
            proto1++;
        }
        //retval.i=env->CallIntMethodA(mObject[GET_REGISTER(vdst & 0x0f)],id_method,reg);
    }
    FINISH(4);
    OP_END;

    HANDLE_OPCODE(OP_AGET)
    {
        jvalue tmpResult;
        u2 arrayInfo;
        EXPORT_PC();
        vdst = INST_AA(inst);
        arrayInfo = FETCH(1);
        vsrc1 = arrayInfo & 0xff;
        vsrc2 = arrayInfo >> 8;
        MY_LOG_VERBOSE("|aget v%d,v%d,v%d", vdst, vsrc1, vsrc2);
        arrayLength = env->GetArrayLength((jarray) allobject.at(GET_REGISTER(vsrc1)).obejct);
        if (GET_REGISTER(vsrc2) >= arrayLength) {
            dvmThrowArrayIndexOutOfBoundsException(env, arrayLength, GET_REGISTER(vsrc2));
            GOTO_exceptionThrown();
        }
        if (strcmp(allobject.at(GET_REGISTER(vsrc1)).proto, "[I") == 0) {
            jint *intarry = env->GetIntArrayElements(
                    (jintArray) allobject.at(GET_REGISTER(vsrc1)).obejct, NULL);
            tmpResult.i = intarry[GET_REGISTER(vsrc2)];
            SET_REGISTER_INT(vdst, tmpResult.i);
        } else if (strcmp(allobject.at(GET_REGISTER(vsrc1)).proto, "[F") == 0) {
            jfloat *floatArray = env->GetFloatArrayElements(
                    (jfloatArray) allobject.at(GET_REGISTER(vsrc1)).obejct, NULL);
            tmpResult.f = floatArray[GET_REGISTER(vsrc2)];
            SET_REGISTER_FLOAT(vdst, tmpResult.f);
        }
    }
    FINISH(2);
    OP_END

    HANDLE_OPCODE(OP_AGET_WIDE)
    {
        jvalue tmpResult;
        u2 arrayInfo;
        EXPORT_PC();
        vdst = INST_AA(inst);
        arrayInfo = FETCH(1);
        vsrc1 = arrayInfo & 0xff;
        vsrc2 = arrayInfo >> 8;
        MY_LOG_VERBOSE("|aget_wide v%d,v%d,v%d", vdst, vsrc1, vsrc2);
        arrayLength = env->GetArrayLength((jarray) allobject.at(GET_REGISTER(vsrc1)).obejct);
        if (GET_REGISTER(vsrc2) >= arrayLength) {
            dvmThrowArrayIndexOutOfBoundsException(env, arrayLength, GET_REGISTER(vsrc2));
            GOTO_exceptionThrown();
        }
        char *typeproto = allobject.at(GET_REGISTER(vsrc1)).proto;
        if (strcmp(allobject.at(GET_REGISTER(vsrc1)).proto, "[D") == 0) {
            jdouble *doubleArray = env->GetDoubleArrayElements(
                    (jdoubleArray) allobject.at(GET_REGISTER(vsrc1)).obejct, NULL);
            tmpResult.d = doubleArray[GET_REGISTER(vsrc2)];
            SET_REGISTER_DOUBLE(vdst, tmpResult.d);
        } else if (strcmp(allobject.at(GET_REGISTER(vsrc1)).proto, "[J") == 0) {
            jlong *longArray = env->GetLongArrayElements(
                    (jlongArray) allobject.at(GET_REGISTER(vsrc1)).obejct, NULL);
            tmpResult.j = longArray[GET_REGISTER(vsrc2)];
            SET_REGISTER_WIDE(vdst, tmpResult.j);
        }

    }
    FINISH(2);
    OP_END
    HANDLE_OPCODE(OP_AGET_OBJECT)
    {
        jvalue tmpResult;
        u2 arrayInfo;
        EXPORT_PC();
        vdst = INST_AA(inst);
        arrayInfo = FETCH(1);
        vsrc1 = arrayInfo & 0xff;
        vsrc2 = arrayInfo >> 8;
        MY_LOG_VERBOSE("|aget_object v%d,v%d,v%d", vdst, vsrc1, vsrc2);
        jobjectArray jobjectArray1 = (jobjectArray) allobject.at(GET_REGISTER(vsrc1)).obejct;

        //jobjectArray jobjectArray1 = (jobjectArray)mtypeArray.at(GET_REGISTER(vsrc1)).marray;
//        jobjectArray object111 = (jobjectArray) (mobject.at(GET_REGISTER(vsrc1)));
        arrayLength = env->GetArrayLength(jobjectArray1);
        if (GET_REGISTER(vsrc2) >= arrayLength) {
            dvmThrowArrayIndexOutOfBoundsException(env, arrayLength, GET_REGISTER(vsrc2));
            GOTO_exceptionThrown();
        }
//        tmpResult.l = env->GetObjectArrayElement((jobjectArray) mobject.at(GET_REGISTER(vsrc1)),
//                                                 GET_REGISTER(vsrc2));
//        tmpResult.l = env->GetObjectArrayElement((jobjectArray) mtypeArray.at(GET_REGISTER(vsrc1)).marray,
//                                                 GET_REGISTER(vsrc2));
        tmpResult.l = env->GetObjectArrayElement(
                (jobjectArray) allobject.at(GET_REGISTER(vsrc1)).obejct,
                GET_REGISTER(vsrc2));
        char *fatherProto = allobject.at(GET_REGISTER(vsrc1)).proto;
        char *childproto = 1 + fatherProto;
        structObject thizObeject;
        thizObeject.obejct = tmpResult.l;
        thizObeject.proto = childproto;
        allobject.push_back(thizObeject);
        //allobject.push_back(tmpResult.l);
        //SET_REGISTER_WIDE(vdst, mobject.size() - 1);
        SET_REGISTER(vdst, allobject.size() - 1);
    }
    FINISH(2);
    OP_END
    HANDLE_OPCODE(OP_AGET_BOOLEAN)
    {
        jvalue tmpResult;
        u2 arrayInfo;
        EXPORT_PC();
        vdst = INST_AA(inst);
        arrayInfo = FETCH(1);
        vsrc1 = arrayInfo & 0xff;
        vsrc2 = arrayInfo >> 8;
        MY_LOG_VERBOSE("|aget v%d,v%d,v%d", vdst, vsrc1, vsrc2);
        arrayLength = env->GetArrayLength((jarray) allobject.at(GET_REGISTER(vsrc1)).obejct);
        if (GET_REGISTER(vsrc2) >= arrayLength) {
            dvmThrowArrayIndexOutOfBoundsException(env, arrayLength, GET_REGISTER(vsrc2));
            GOTO_exceptionThrown();
        }
        jboolean *booleanArray = env->GetBooleanArrayElements(
                (jbooleanArray) allobject.at(GET_REGISTER(vsrc1)).obejct, NULL);
        tmpResult.z = booleanArray[GET_REGISTER(vsrc2)];
        SET_REGISTER(vdst, tmpResult.z);

    }
    FINISH(2);
    OP_END
    HANDLE_OPCODE(OP_AGET_BYTE)
    {
        jvalue tmpResult;
        u2 arrayInfo;
        EXPORT_PC();
        vdst = INST_AA(inst);
        arrayInfo = FETCH(1);
        vsrc1 = arrayInfo & 0xff;
        vsrc2 = arrayInfo >> 8;
        MY_LOG_VERBOSE("|aget v%d,v%d,v%d", vdst, vsrc1, vsrc2);
        arrayLength = env->GetArrayLength((jarray) allobject.at(GET_REGISTER(vsrc1)).obejct);
        if (GET_REGISTER(vsrc2) >= arrayLength) {
            dvmThrowArrayIndexOutOfBoundsException(env, arrayLength, GET_REGISTER(vsrc2));
            GOTO_exceptionThrown();
        }
        jbyte *byteArray = env->GetByteArrayElements(
                (jbyteArray) allobject.at(GET_REGISTER(vsrc1)).obejct, NULL);
        tmpResult.b = byteArray[GET_REGISTER(vsrc2)];
        SET_REGISTER(vdst, tmpResult.b);

    }
    FINISH(2);
    OP_END
    HANDLE_OPCODE(OP_AGET_CHAR)
    {
        jvalue tmpResult;
        u2 arrayInfo;
        EXPORT_PC();
        vdst = INST_AA(inst);
        arrayInfo = FETCH(1);
        vsrc1 = arrayInfo & 0xff;
        vsrc2 = arrayInfo >> 8;
        MY_LOG_VERBOSE("|aget v%d,v%d,v%d", vdst, vsrc1, vsrc2);
        arrayLength = env->GetArrayLength((jarray) allobject.at(GET_REGISTER(vsrc1)).obejct);
        if (GET_REGISTER(vsrc2) >= arrayLength) {
            dvmThrowArrayIndexOutOfBoundsException(env, arrayLength, GET_REGISTER(vsrc2));
            GOTO_exceptionThrown();
        }
        jchar *charArray = env->GetCharArrayElements(
                (jcharArray) allobject.at(GET_REGISTER(vsrc1)).obejct, NULL);
        tmpResult.c = charArray[GET_REGISTER(vsrc2)];
        SET_REGISTER(vdst, tmpResult.c);
    }
    FINISH(2);
    OP_END
    HANDLE_OPCODE(OP_AGET_SHORT)
    {
        jvalue tmpResult;
        u2 arrayInfo;
        EXPORT_PC();
        vdst = INST_AA(inst);
        arrayInfo = FETCH(1);
        vsrc1 = arrayInfo & 0xff;
        vsrc2 = arrayInfo >> 8;
        MY_LOG_VERBOSE("|aget v%d,v%d,v%d", vdst, vsrc1, vsrc2);
        arrayLength = env->GetArrayLength((jarray) allobject.at(GET_REGISTER(vsrc1)).obejct);
        if (GET_REGISTER(vsrc2) >= arrayLength) {
            dvmThrowArrayIndexOutOfBoundsException(env, arrayLength, GET_REGISTER(vsrc2));
            GOTO_exceptionThrown();
        }
        jshort *shortArray = env->GetShortArrayElements(
                (jshortArray) allobject.at(GET_REGISTER(vsrc1)).obejct, NULL);
        tmpResult.s = shortArray[GET_REGISTER(vsrc2)];
        SET_REGISTER(vdst, tmpResult.s);
    }
    FINISH(2);
    OP_END
///* File: c/OP_AGET_WIDE.cpp */
//HANDLE_OP_AGET(OP_AGET_WIDE, "-wide", s8, _WIDE)
//    OP_END
//
///* File: c/OP_AGET_OBJECT.cpp */
//HANDLE_OP_AGET(OP_AGET_OBJECT, "-object", u4,)
//    OP_END
//
///* File: c/OP_AGET_BOOLEAN.cpp */
//HANDLE_OP_AGET(OP_AGET_BOOLEAN, "-boolean", u1,)
//    OP_END
//
///* File: c/OP_AGET_BYTE.cpp */
//HANDLE_OP_AGET(OP_AGET_BYTE, "-byte", s1,)
//    OP_END
//
///* File: c/OP_AGET_CHAR.cpp */
//HANDLE_OP_AGET(OP_AGET_CHAR, "-char", u2,)
//    OP_END
//
///* File: c/OP_AGET_SHORT.cpp */
//HANDLE_OP_AGET(OP_AGET_SHORT, "-short", s2,)
//    OP_END

///* File: c/OP_APUT.cpp */
//HANDLE_OP_APUT(OP_APUT, "", u4, )
//    OP_END
    HANDLE_OPCODE(OP_APUT)
    {
        jvalue tmpResult;
        jarray arrayObj;
        u2 arrayInfo;
        EXPORT_PC();
        vdst = INST_AA(inst);       /* AA: source valueҪ�����õ�ֵ���ڵļĴ��� */
        arrayInfo = FETCH(1);
        vsrc1 = arrayInfo & 0xff;   /* BB: ����ĵ�ַ */
        vsrc2 = arrayInfo >> 8;     /* CC: ����ĵڼ���λ�� */
        MY_LOG_VERBOSE("|aput v%d,v%d,v%d", vdst, vsrc1, vsrc2);
        arrayObj = (jarray) allobject.at(GET_REGISTER(vsrc1)).obejct;
        //allobject.at(GET_REGISTER(vsrc1)).proto="[I";
        // arrayObj = mtypeArray.at(GET_REGISTER(vsrc1)).marray;
//        arrayObj  = (jobjectArray)mobject.at(GET_REGISTER(vsrc1));
        if (GET_REGISTER(vsrc2) >= env->GetArrayLength(arrayObj)) {
            dvmThrowArrayIndexOutOfBoundsException(env, env->GetArrayLength(arrayObj),
                                                   GET_REGISTER(vsrc2));
            GOTO_exceptionThrown();
        }
        if (strcmp(allobject.at(GET_REGISTER(vsrc1)).proto, "[I") == 0) {
            jint *intarry = env->GetIntArrayElements(
                    (jintArray) allobject.at(GET_REGISTER(vsrc1)).obejct, NULL);
            intarry[GET_REGISTER(vsrc2)] = (jint) GET_REGISTER_INT(vdst);
            env->ReleaseIntArrayElements((jintArray) allobject.at(GET_REGISTER(vsrc1)).obejct,
                                         intarry, 0);
        } else if (strcmp(allobject.at(GET_REGISTER(vsrc1)).proto, "[F") == 0) {
            jfloat *floatArray = env->GetFloatArrayElements(
                    (jfloatArray) allobject.at(GET_REGISTER(vsrc1)).obejct, NULL);
            floatArray[GET_REGISTER(vsrc2)] = (jfloat) GET_REGISTER_FLOAT(vdst);
            env->ReleaseFloatArrayElements((jfloatArray) allobject.at(GET_REGISTER(vsrc1)).obejct,
                                           floatArray, 0);
        }
//        if (strcmp(mtypeArray.at(GET_REGISTER(vsrc1)).proto, "[I") == 0) {
//            jint *intarry = env->GetIntArrayElements(
//                    (jintArray) mtypeArray.at(GET_REGISTER(vsrc1)).marray, NULL);
//            intarry[GET_REGISTER(vsrc2)] = (jint) GET_REGISTER_INT(vdst);
//            env->ReleaseIntArrayElements((jintArray) mtypeArray.at(GET_REGISTER(vsrc1)).marray,
//                                         intarry, 0);
//        } else if (strcmp(mtypeArray.at(GET_REGISTER(vsrc1)).proto, "[F") == 0) {
//            jfloat *floatArray = env->GetFloatArrayElements(
//                    (jfloatArray) mtypeArray.at(GET_REGISTER(vsrc1)).marray, NULL);
//            floatArray[GET_REGISTER(vsrc2)] = (jfloat) GET_REGISTER_FLOAT(vdst);
//            env->ReleaseFloatArrayElements((jfloatArray) mtypeArray.at(GET_REGISTER(vsrc1)).marray,
//                                           floatArray, 0);
//        }
    }
    FINISH(2);
    OP_END

    HANDLE_OPCODE(OP_APUT_OBJECT)
    {
        jvalue tmpResult;
        u2 arrayInfo;
        EXPORT_PC();
        vdst = INST_AA(inst);
        arrayInfo = FETCH(1);
        vsrc1 = arrayInfo & 0xff;
        vsrc2 = arrayInfo >> 8;
        MY_LOG_VERBOSE("|OP_APUT_OBJECT v%d,v%d,v%d", vdst, vsrc1, vsrc2);
        arrayLength = env->GetArrayLength((jarray) allobject.at(GET_REGISTER(vsrc1)).obejct);
        if (GET_REGISTER(vsrc2) >= arrayLength) {
            dvmThrowArrayIndexOutOfBoundsException(env, arrayLength, GET_REGISTER(vsrc2));
            GOTO_exceptionThrown();
        }
        jobjectArray mobjectArray = (jobjectArray) allobject.at(vsrc1).obejct;
        env->SetObjectArrayElement(mobjectArray, GET_REGISTER(vsrc2),
                                   allobject.at(GET_REGISTER(vdst)).obejct);
    }
    FINISH(2);
    OP_END
    HANDLE_OPCODE(OP_APUT_WIDE)
    {
        jvalue tmpResult;
        u2 arrayInfo;
        EXPORT_PC();
        vdst = INST_AA(inst);
        arrayInfo = FETCH(1);
        vsrc1 = arrayInfo & 0xff;
        vsrc2 = arrayInfo >> 8;
        MY_LOG_VERBOSE("|APUT_WIDE v%d,v%d,v%d", vdst, vsrc1, vsrc2);
        arrayLength = env->GetArrayLength((jarray) allobject.at(GET_REGISTER(vsrc1)).obejct);
        if (GET_REGISTER(vsrc2) >= arrayLength) {
            dvmThrowArrayIndexOutOfBoundsException(env, arrayLength, GET_REGISTER(vsrc2));
            GOTO_exceptionThrown();
        }
        if (strcmp(allobject.at(GET_REGISTER(vsrc1)).proto, "[D") == 0) {
            jdouble *doubleArray = env->GetDoubleArrayElements(
                    (jdoubleArray) allobject.at(GET_REGISTER(vsrc1)).obejct, NULL);
            doubleArray[GET_REGISTER(vsrc2)] = (jdouble) GET_REGISTER_DOUBLE(vdst);
            env->ReleaseDoubleArrayElements(
                    (jdoubleArray) allobject.at(GET_REGISTER(vsrc1)).obejct, doubleArray, 0);
        } else if (strcmp(allobject.at(GET_REGISTER(vsrc1)).proto, "[J") == 0) {
            jlong *longArray = env->GetLongArrayElements(
                    (jlongArray) allobject.at(GET_REGISTER(vsrc1)).obejct, NULL);
            longArray[GET_REGISTER(vsrc2)] = (jlong) GET_REGISTER_WIDE(vdst);
            env->ReleaseLongArrayElements((jlongArray) allobject.at(GET_REGISTER(vsrc1)).obejct,
                                          longArray, 0);
        }
    }
    FINISH(2);
    OP_END


    HANDLE_OPCODE(OP_APUT_BOOLEAN)
    {
        jvalue tmpResult;
        u2 arrayInfo;
        EXPORT_PC();
        vdst = INST_AA(inst);
        arrayInfo = FETCH(1);
        vsrc1 = arrayInfo & 0xff;
        vsrc2 = arrayInfo >> 8;
        MY_LOG_VERBOSE("|OP_APUT_OBJECT v%d,v%d,v%d", vdst, vsrc1, vsrc2);
        arrayLength = env->GetArrayLength((jarray) allobject.at(GET_REGISTER(vsrc1)).obejct);
        if (GET_REGISTER(vsrc2) >= arrayLength) {
            dvmThrowArrayIndexOutOfBoundsException(env, arrayLength, GET_REGISTER(vsrc2));
            GOTO_exceptionThrown();
        }
        jboolean *booleanArray = env->GetBooleanArrayElements(
                (jbooleanArray) allobject.at(GET_REGISTER(vsrc1)).obejct, NULL);
        booleanArray[GET_REGISTER(vsrc2)] = (jboolean) GET_REGISTER_WIDE(vdst);
        env->ReleaseBooleanArrayElements((jbooleanArray) allobject.at(GET_REGISTER(vsrc1)).obejct,
                                         booleanArray, 0);
    }
    FINISH(2);


    HANDLE_OPCODE(OP_APUT_BYTE)
    {
        jvalue tmpResult;
        u2 arrayInfo;
        EXPORT_PC();
        vdst = INST_AA(inst);
        arrayInfo = FETCH(1);
        vsrc1 = arrayInfo & 0xff;
        vsrc2 = arrayInfo >> 8;
        MY_LOG_VERBOSE("|OP_APUT_OBJECT v%d,v%d,v%d", vdst, vsrc1, vsrc2);
        arrayLength = env->GetArrayLength((jarray) allobject.at(GET_REGISTER(vsrc1)).obejct);
        if (GET_REGISTER(vsrc2) >= arrayLength) {
            dvmThrowArrayIndexOutOfBoundsException(env, arrayLength, GET_REGISTER(vsrc2));
            GOTO_exceptionThrown();
        }
        jbyte *byteArray = env->GetByteArrayElements(
                (jbyteArray) allobject.at(GET_REGISTER(vsrc1)).obejct, NULL);
        byteArray[GET_REGISTER(vsrc2)] = (jbyte) GET_REGISTER(vdst);
        env->ReleaseByteArrayElements((jbyteArray) allobject.at(GET_REGISTER(vsrc1)).obejct,
                                      byteArray, 0);
    }
    FINISH(2);
    OP_END
    HANDLE_OPCODE(OP_APUT_CHAR)
    {
        jvalue tmpResult;
        u2 arrayInfo;
        EXPORT_PC();
        vdst = INST_AA(inst);
        arrayInfo = FETCH(1);
        vsrc1 = arrayInfo & 0xff;
        vsrc2 = arrayInfo >> 8;
        MY_LOG_VERBOSE("|OP_APUT_OBJECT v%d,v%d,v%d", vdst, vsrc1, vsrc2);
        arrayLength = env->GetArrayLength((jarray) allobject.at(GET_REGISTER(vsrc1)).obejct);
        if (GET_REGISTER(vsrc2) >= arrayLength) {
            dvmThrowArrayIndexOutOfBoundsException(env, arrayLength, GET_REGISTER(vsrc2));
            GOTO_exceptionThrown();
        }
        jchar *charArray = env->GetCharArrayElements(
                (jcharArray) allobject.at(GET_REGISTER(vsrc1)).obejct, NULL);
        charArray[GET_REGISTER(vsrc2)] = (jchar) GET_REGISTER(vdst);
        env->ReleaseCharArrayElements((jcharArray) allobject.at(GET_REGISTER(vsrc1)).obejct,
                                      charArray, 0);
    }
    FINISH(2);
    OP_END

    HANDLE_OPCODE(OP_APUT_SHORT)
    {
        jvalue tmpResult;
        u2 arrayInfo;
        EXPORT_PC();
        vdst = INST_AA(inst);
        arrayInfo = FETCH(1);
        vsrc1 = arrayInfo & 0xff;
        vsrc2 = arrayInfo >> 8;
        MY_LOG_VERBOSE("|OP_APUT_OBJECT v%d,v%d,v%d", vdst, vsrc1, vsrc2);
        arrayLength = env->GetArrayLength((jarray) allobject.at(GET_REGISTER(vsrc1)).obejct);
        if (GET_REGISTER(vsrc2) >= arrayLength) {
            dvmThrowArrayIndexOutOfBoundsException(env, arrayLength, GET_REGISTER(vsrc2));
            GOTO_exceptionThrown();
        }
        jshort *shortArray = env->GetShortArrayElements(
                (jshortArray) allobject.at(GET_REGISTER(vsrc1)).obejct, NULL);
        shortArray[GET_REGISTER(vsrc2)] = (jshort) GET_REGISTER(vdst);
        env->ReleaseShortArrayElements((jshortArray) allobject.at(GET_REGISTER(vsrc1)).obejct,
                                       shortArray, 0);
    }
    FINISH(2);
    OP_END
///* File: c/OP_APUT_WIDE.cpp */
//HANDLE_OP_APUT(OP_APUT_WIDE, "-wide", s8, _WIDE)
//    OP_END
//
//    /* File: c/OP_APUT_BOOLEAN.cpp */
//HANDLE_OP_APUT(OP_APUT_BOOLEAN, "-boolean", u1,)
//    OP_END
//
///* File: c/OP_APUT_BYTE.cpp */
//HANDLE_OP_APUT(OP_APUT_BYTE, "-byte", s1,)
//    OP_END
//
///* File: c/OP_APUT_CHAR.cpp */
//HANDLE_OP_APUT(OP_APUT_CHAR, "-char", u2,)
//    OP_END
//
///* File: c/OP_APUT_SHORT.cpp */
//HANDLE_OP_APUT(OP_APUT_SHORT, "-short", s2,)
    OP_END

/* File: c/OP_IGET.cpp */
//HANDLE_IGET_X(OP_IGET, "", Int,)
//    OP_END
    HANDLE_OPCODE(OP_IGET)
    {
        vdst = INST_A(inst);vsrc1 = INST_B(inst);
        orgref = FETCH(1);curref = FETCH(2);
        MY_LOG_VERBOSE("|iget v%d,v%d,field@0x%04x", vdst, vsrc1, orgref);
        int fieldNameIdx = ycFile->mYcFormat.mFieldItem[curref].fieldNameIdx;
        int typeIdx = ycFile->mYcFormat.mFieldItem[curref].typeIdx;
        int classNameIdx = ycFile->mYcFormat.mFieldItem[curref].classNameIdx;
        char *className = ycFile->mYcFormat.mStringItem[classNameIdx];
        char *typeName = ycFile->mYcFormat.mStringItem[typeIdx];
        char *fieldName = ycFile->mYcFormat.mStringItem[fieldNameIdx];
        jclass jclass = env->FindClass(className);
        jfieldID jfieldID1 = env->GetFieldID(jclass, fieldName, typeName);
        if (strcmp(typeName, "I") == 0) {
            jint mint = env->GetIntField(allobject.at(GET_REGISTER(vsrc1)).obejct, jfieldID1);
            SET_REGISTER(vdst, mint);
        }
        if (strcmp(typeName, "F") == 0) {
            jfloat mint = env->GetFloatField(allobject.at(GET_REGISTER(vsrc1)).obejct, jfieldID1);
            SET_REGISTER_FLOAT(vdst, mint);
        }
    }
    FINISH(3);
    OP_END

/* File: c/OP_IGET_WIDE.cpp */
//HANDLE_IGET_X(OP_IGET_WIDE, "-wide", Long, _WIDE)
//    OP_END
    HANDLE_OPCODE(OP_IGET_WIDE)
    {
        vdst = INST_A(inst);
        vsrc1 = INST_B(inst);
        orgref = FETCH(1);
        curref = FETCH(2);
        MY_LOG_VERBOSE("|iget_wide v%d,v%d,field@0x%04x", vdst, vsrc1, orgref);
//        jobject thisobject = mobject.at(GET_REGISTER(vsrc1));
        int fieldNameIdx = ycFile->mYcFormat.mFieldItem[curref].fieldNameIdx;
        MY_LOG_VERBOSE("|fieldNameIdx %d", fieldNameIdx);
        int typeIdx = ycFile->mYcFormat.mFieldItem[curref].typeIdx;
        MY_LOG_VERBOSE("|typeIdx %d", typeIdx);
        int classNameIdx = ycFile->mYcFormat.mFieldItem[curref].classNameIdx;
        MY_LOG_VERBOSE("|classNameIdx %d", classNameIdx);
        char *className = ycFile->mYcFormat.mStringItem[classNameIdx];
        MY_LOG_VERBOSE("|className %s", className);
        char *typeName = ycFile->mYcFormat.mStringItem[typeIdx];
        MY_LOG_VERBOSE("|typeName %s", typeName);
        char *fieldName = ycFile->mYcFormat.mStringItem[fieldNameIdx];
        MY_LOG_VERBOSE("|fieldName %s", fieldName);
        jclass jclass = env->FindClass(className);
        MY_LOG_VERBOSE("|jclass found ");
        jfieldID jfieldID1 = env->GetFieldID(jclass, fieldName, typeName);
        MY_LOG_VERBOSE("|jfieldID found");
        //jint mint = env->GetIntField(thiz, jfieldID1);
        if (strcmp(typeName, "J") == 0) {
            jlong mlong = env->GetLongField(allobject.at(GET_REGISTER(vsrc1)).obejct, jfieldID1);                                 \
           SET_REGISTER_WIDE(vdst, mlong);
            MY_LOG_VERBOSE("|OP_IGET_WIDE found");
        }
        if (strcmp(typeName, "D") == 0) {
            jdouble mdouble = env->GetDoubleField(allobject.at(GET_REGISTER(vsrc1)).obejct,
                                                  jfieldID1);                                 \
             SET_REGISTER_DOUBLE(vdst, mdouble);
            MY_LOG_VERBOSE("|OP_IGET_WIDE found");
        }
    }
    FINISH(3);
    OP_END

/* File: c/OP_IGET_OBJECT.cpp */
HANDLE_IGET_X(OP_IGET_OBJECT, "-object", Object, _AS_OBJECT)
    OP_END


/* File: c/OP_IGET_BOOLEAN.cpp */
HANDLE_IGET_X(OP_IGET_BOOLEAN, "", Int,)
    OP_END

/* File: c/OP_IGET_BYTE.cpp */
HANDLE_IGET_X(OP_IGET_BYTE, "", Int,)
    OP_END

/* File: c/OP_IGET_CHAR.cpp */
HANDLE_IGET_X(OP_IGET_CHAR, "", Int,)
    OP_END

/* File: c/OP_IGET_SHORT.cpp */
HANDLE_IGET_X(OP_IGET_SHORT, "", Int,)
    OP_END

/* File: c/OP_IPUT.cpp */
//HANDLE_IPUT_X(OP_IPUT, "", Int,)
//    OP_END
    HANDLE_OPCODE(OP_IPUT)
    {
        vdst = INST_A(inst);
        vsrc1 = INST_B(inst);
        orgref = FETCH(1);
        curref = FETCH(2);
        MY_LOG_VERBOSE("|iput v%d,v%d,field@0x%04x", vdst, vsrc1, orgref);
        jobject instance = allobject.at(GET_REGISTER(vsrc1)).obejct;
        int classnameIdx = ycFile->mYcFormat.mFieldItem[curref].classNameIdx;
        MY_LOG_VERBOSE("|classNameIdx %d", classnameIdx);
        int typenameIdx = ycFile->mYcFormat.mFieldItem[curref].typeIdx;
        MY_LOG_VERBOSE("|typeIdx %d", typenameIdx);
        int fieldnameIdx = ycFile->mYcFormat.mFieldItem[curref].fieldNameIdx;
        MY_LOG_VERBOSE("|fieldNameIdx %d", fieldnameIdx);
        char *classname = ycFile->mYcFormat.mStringItem[classnameIdx];
        MY_LOG_VERBOSE("|className %s", classname);
        char *typeName = ycFile->mYcFormat.mStringItem[typenameIdx];
        MY_LOG_VERBOSE("|typeName %s", typeName);
        char *fieldname = ycFile->mYcFormat.mStringItem[fieldnameIdx];
        MY_LOG_VERBOSE("|fieldName %s", fieldname);
        jclass jclass1 = env->FindClass(classname);
        MY_LOG_VERBOSE("|jclass found ");
        jfieldID jfieldID1 = env->GetFieldID(jclass1, fieldname, typeName);
        MY_LOG_VERBOSE("|jfieldID found");
        if (strcmp(typeName, "I") == 0) {
            jint value = GET_REGISTER(vdst);
            env->SetIntField(instance, jfieldID1, value);
        }
        if (strcmp(typeName, "F") == 0) {
            jfloat value = GET_REGISTER_FLOAT(vdst);
            env->SetFloatField(instance, jfieldID1, value);
        }

    }
    FINISH(3);
    OP_END

/* File: c/OP_IPUT_WIDE.cpp */
//HANDLE_IPUT_X(OP_IPUT_WIDE, "-wide", Long, _WIDE)
//    OP_END
    HANDLE_OPCODE(OP_IPUT_WIDE)
    {
        vdst = INST_A(inst);
        vsrc1 = INST_B(inst);
        orgref = FETCH(1);
        curref = FETCH(2);
        MY_LOG_VERBOSE("|iput_wide v%d,v%d,field@0x%04x", vdst, vsrc1, orgref);
        jobject instance = allobject.at(GET_REGISTER(vsrc1)).obejct;
        int classnameIdx = ycFile->mYcFormat.mFieldItem[curref].classNameIdx;
        MY_LOG_VERBOSE("|classNameIdx %d", classnameIdx);
        int typenameIdx = ycFile->mYcFormat.mFieldItem[curref].typeIdx;
        MY_LOG_VERBOSE("|typeIdx %d", typenameIdx);
        int fieldnameIdx = ycFile->mYcFormat.mFieldItem[curref].fieldNameIdx;
        MY_LOG_VERBOSE("|fieldNameIdx %d", fieldnameIdx);
        char *classname = ycFile->mYcFormat.mStringItem[classnameIdx];
        MY_LOG_VERBOSE("|className %s", classname);
        char *typeName = ycFile->mYcFormat.mStringItem[typenameIdx];
        MY_LOG_VERBOSE("|typeName %s", typeName);
        char *fieldname = ycFile->mYcFormat.mStringItem[fieldnameIdx];
        MY_LOG_VERBOSE("|fieldName %s", fieldname);
        jclass jclass1 = env->FindClass(classname);
        MY_LOG_VERBOSE("|jclass found ");
        jfieldID jfieldID1 = env->GetFieldID(jclass1, fieldname, typeName);
        MY_LOG_VERBOSE("|jfieldID found");
        if (strcmp(typeName, "J") == 0) {
            jlong value = GET_REGISTER_WIDE(vdst);
            env->SetLongField(instance, jfieldID1, value);
        }
        if (strcmp(typeName, "D") == 0) {
            jdouble value = GET_REGISTER_DOUBLE(vdst);
            env->SetDoubleField(instance, jfieldID1, value);
        }
    }
    FINISH(3);
    OP_END
/* File: c/OP_IPUT_OBJECT.cpp */
/*
 * The VM spec says we should verify that the reference being stored into
 * the field is assignment compatible.  In practice, many popular VMs don't
 * do this because it slows down a very common operation.  It's not so bad
 * for us, since "dexopt" quickens it whenever possible, but it's still an
 * issue.
 *
 * To make this spec-complaint, we'd need to add a ClassObject pointer to
 * the Field struct, resolve the field's type descriptor at link or class
 * init time, and then verify the type here.
 */
//    HANDLE_IPUT_X(OP_IPUT_OBJECT,           "-object", Object, _AS_OBJECT)
//    OP_END
    HANDLE_OPCODE(OP_IPUT_OBJECT)
    {
        vdst = INST_A(inst);
        vsrc1 = INST_B(inst);
        orgref = FETCH(1);
        curref = FETCH(2);
        MY_LOG_VERBOSE("|iput v%d,v%d,field@0x%04x", vdst, vsrc1, orgref);
        jobject instance = allobject.at(GET_REGISTER(vsrc1)).obejct;
        int classnameIdx = ycFile->mYcFormat.mFieldItem[curref].classNameIdx;
        MY_LOG_VERBOSE("|classNameIdx %d", classnameIdx);
        int typenameIdx = ycFile->mYcFormat.mFieldItem[curref].typeIdx;
        MY_LOG_VERBOSE("|typeIdx %d", typenameIdx);
        int fieldnameIdx = ycFile->mYcFormat.mFieldItem[curref].fieldNameIdx;
        MY_LOG_VERBOSE("|fieldNameIdx %d", fieldnameIdx);
        char *classname = ycFile->mYcFormat.mStringItem[classnameIdx];
        MY_LOG_VERBOSE("|className %s", classname);
        char *typeName = ycFile->mYcFormat.mStringItem[typenameIdx];
        MY_LOG_VERBOSE("|typeName %s", typeName);
        char *fieldname = ycFile->mYcFormat.mStringItem[fieldnameIdx];
        MY_LOG_VERBOSE("|fieldName %s", fieldname);
        jclass jclass1 = env->FindClass(classname);
        MY_LOG_VERBOSE("|jclass found ");
        jfieldID jfieldID1 = env->GetFieldID(jclass1, fieldname, typeName);
        MY_LOG_VERBOSE("|jfieldID found");
        jobject value = allobject.at(GET_REGISTER(vdst)).obejct;
        env->SetObjectField(instance, jfieldID1, value);
    }
    FINISH(3);
    OP_END

/* File: c/OP_IPUT_BOOLEAN.cpp */
HANDLE_IPUT_X(OP_IPUT_BOOLEAN, "", Int,)
    OP_END

/* File: c/OP_IPUT_BYTE.cpp */
HANDLE_IPUT_X(OP_IPUT_BYTE, "", Int,)
    OP_END

/* File: c/OP_IPUT_CHAR.cpp */
HANDLE_IPUT_X(OP_IPUT_CHAR, "", Int,)
    OP_END

/* File: c/OP_IPUT_SHORT.cpp */
HANDLE_IPUT_X(OP_IPUT_SHORT, "", Int,)
    OP_END
///* File: c/OP_SGET.cpp */
//HANDLE_SGET_X(OP_SGET, "", Int,)
//    OP_END
//
///* File: c/OP_SGET_WIDE.cpp */
//HANDLE_SGET_X(OP_SGET_WIDE, "-wide", Long, _WIDE)
//    OP_END
    HANDLE_OPCODE(OP_SGET)
    {
        vdst = INST_AA(inst);
        orgref = FETCH(1);
        curref = FETCH(2);
        MY_LOG_VERBOSE("|sget v%d,sfield@0x%04x", vdst, orgref);
        int fieldNameIdx = ycFile->mYcFormat.mFieldItem[curref].fieldNameIdx;
        MY_LOG_VERBOSE("|fieldNameIdx %d", fieldNameIdx);
        int typeIdx = ycFile->mYcFormat.mFieldItem[curref].typeIdx;
        MY_LOG_VERBOSE("|typeIdx %d", typeIdx);
        int classNameIdx = ycFile->mYcFormat.mFieldItem[curref].classNameIdx;
        MY_LOG_VERBOSE("|classNameIdx %d", classNameIdx);
        char *className = ycFile->mYcFormat.mStringItem[classNameIdx];
        MY_LOG_VERBOSE("|className %s", className);
        char *typeName = ycFile->mYcFormat.mStringItem[typeIdx];
        MY_LOG_VERBOSE("|typeName %s", typeName);
        char *fieldName = ycFile->mYcFormat.mStringItem[fieldNameIdx];
        MY_LOG_VERBOSE("|fieldName %s", fieldName);
        jclass jclass = env->FindClass(className);
        MY_LOG_VERBOSE("|jclass found ");
        jfieldID jfieldID = env->GetStaticFieldID(jclass, fieldName, typeName);
        MY_LOG_VERBOSE("|jfieldID found");
        if (strcmp(typeName, "I") == 0) {
            jint mint = env->GetStaticIntField(jclass, jfieldID);
            SET_REGISTER(vdst, mint);
        }
        if (strcmp(typeName, "F") == 0) {
            jfloat mint = env->GetStaticFloatField(jclass, jfieldID);
            SET_REGISTER_FLOAT(vdst, mint);
        }
    }
    FINISH(3);
    OP_END

/* File: c/OP_SGET_WIDE.cpp */
//HANDLE_SGET_X(OP_SGET_WIDE, "-wide", Long, _WIDE)
//    OP_END
    HANDLE_OPCODE(OP_SGET_WIDE)
    {
        vdst = INST_AA(inst);
        orgref = FETCH(1);
        curref = FETCH(2);
        MY_LOG_VERBOSE("|sget_wide v%d,sfield@0x%04x", vdst, orgref);
        int fieldNameIdx = ycFile->mYcFormat.mFieldItem[curref].fieldNameIdx;
        MY_LOG_VERBOSE("|fieldNameIdx %d", fieldNameIdx);
        int typeIdx = ycFile->mYcFormat.mFieldItem[curref].typeIdx;
        MY_LOG_VERBOSE("|typeIdx %d", typeIdx);
        int classNameIdx = ycFile->mYcFormat.mFieldItem[curref].classNameIdx;
        MY_LOG_VERBOSE("|classNameIdx %d", classNameIdx);
        char *className = ycFile->mYcFormat.mStringItem[classNameIdx];
        MY_LOG_VERBOSE("|className %s", className);
        char *typeName = ycFile->mYcFormat.mStringItem[typeIdx];
        MY_LOG_VERBOSE("|typeName %s", typeName);
        char *fieldName = ycFile->mYcFormat.mStringItem[fieldNameIdx];
        MY_LOG_VERBOSE("|fieldName %s", fieldName);
        jclass jclass = env->FindClass(className);
        MY_LOG_VERBOSE("|jclass found ");
        jfieldID jfieldID = env->GetStaticFieldID(jclass, fieldName, typeName);
        MY_LOG_VERBOSE("|jfieldID found");
        if (strcmp(typeName, "J") == 0) {
            jlong mlong = env->GetStaticLongField(jclass, jfieldID);                   \
        SET_REGISTER_WIDE(vdst, mlong);
        }
        if (strcmp(typeName, "D") == 0) {
            jdouble mdouble = env->GetStaticDoubleField(jclass, jfieldID);                   \
            SET_REGISTER_DOUBLE(vdst, mdouble);
        }
    }
    FINISH(3);
    OP_END
/* File: c/OP_SGET_OBJECT.cpp */
    HANDLE_OPCODE(OP_SGET_OBJECT)
    {
        vdst = INST_AA(inst);
        orgref = FETCH(1);
        curref = FETCH(2);
        //MY_LOG_VERBOSE("|sget%s v%d,sfield@0x%04x",(_opname),vdst, orgref);
        int fieldNameIdx = ycFile->mYcFormat.mFieldItem[curref].fieldNameIdx;
        MY_LOG_VERBOSE("|fieldNameIdx %d", fieldNameIdx);
        int typeIdx = ycFile->mYcFormat.mFieldItem[curref].typeIdx;
        MY_LOG_VERBOSE("|typeIdx %d", typeIdx);
        int classNameIdx = ycFile->mYcFormat.mFieldItem[curref].classNameIdx;
        MY_LOG_VERBOSE("|classNameIdx %d", classNameIdx);
        char *className = ycFile->mYcFormat.mStringItem[classNameIdx];
        MY_LOG_VERBOSE("|className %s", className);
        char *typeName = ycFile->mYcFormat.mStringItem[typeIdx];
        MY_LOG_VERBOSE("|typeName %s", typeName);
        char *fieldName = ycFile->mYcFormat.mStringItem[fieldNameIdx];
        MY_LOG_VERBOSE("|fieldName %s", fieldName);
        jclass jclass = env->FindClass(className);
        MY_LOG_VERBOSE("|jclass found ");
        jfieldID jfieldID = env->GetStaticFieldID(jclass, fieldName, typeName);
        MY_LOG_VERBOSE("|jfieldID found");
        structObject thizObject;
        thizObject.obejct = env->GetStaticObjectField(jclass, jfieldID);
        thizObject.proto = "L";
        allobject.push_back(thizObject);

//        mobject.push_back(env->GetStaticObjectField(jclass,jfieldID));
        MY_LOG_VERBOSE("|GetObjectField successed");
        MY_LOG_VERBOSE("| mObject realloc successed");
        //SET_REGISTER(vdst,mobject.size()-1);
        SET_REGISTER(vdst, allobject.size() - 1);

    }
    FINISH(3);
//HANDLE_SGET_X(OP_SGET_OBJECT, "-object", Object, _AS_OBJECT)
//    OP_END

/* File: c/OP_SGET_BOOLEAN.cpp */
HANDLE_SGET_X(OP_SGET_BOOLEAN, "", Int,)
    OP_END

/* File: c/OP_SGET_BYTE.cpp */
HANDLE_SGET_X(OP_SGET_BYTE, "", Int,)
    OP_END

/* File: c/OP_SGET_CHAR.cpp */
HANDLE_SGET_X(OP_SGET_CHAR, "", Int,)
    OP_END

/* File: c/OP_SGET_SHORT.cpp */
HANDLE_SGET_X(OP_SGET_SHORT, "", Int,)
    OP_END

/* File: c/OP_SPUT.cpp */
//HANDLE_SPUT_X(OP_SPUT, "", Int,)
//    OP_END
//
/* File: c/OP_SPUT_WIDE.cpp */
//HANDLE_SPUT_X(OP_SPUT_WIDE, "-wide", Long, _WIDE)
//    OP_END
    HANDLE_OPCODE(OP_SPUT)
    {
        vdst = INST_AA(inst);
        orgref = FETCH(1);
        curref = FETCH(2);
        MY_LOG_VERBOSE("|sput v%d,sfield@0x%04x", vdst, orgref);
        int classnameIdx = ycFile->mYcFormat.mFieldItem[curref].classNameIdx;
        MY_LOG_VERBOSE("|classNameIdx %d", classnameIdx);
        int typenameIdx = ycFile->mYcFormat.mFieldItem[curref].typeIdx;
        MY_LOG_VERBOSE("|typeIdx %d", typenameIdx);
        int fieldnameIdx = ycFile->mYcFormat.mFieldItem[curref].fieldNameIdx;
        MY_LOG_VERBOSE("|fieldNameIdx %d", fieldnameIdx);
        char *classname = ycFile->mYcFormat.mStringItem[classnameIdx];
        MY_LOG_VERBOSE("|className %s", classname);
        char *typeName = ycFile->mYcFormat.mStringItem[typenameIdx];
        MY_LOG_VERBOSE("|typeName %s", typeName);
        char *fieldname = ycFile->mYcFormat.mStringItem[fieldnameIdx];
        MY_LOG_VERBOSE("|fieldName %s", fieldname);
        jclass jclass1 = env->FindClass(classname);
        jfieldID jfieldID1 = env->GetStaticFieldID(jclass1, fieldname, typeName);
        if (strcmp(typeName, "I") == 0) {
            jint value = GET_REGISTER(vdst);
            env->SetStaticIntField(jclass1, jfieldID1, value);
        }
        if (strcmp(typeName, "F") == 0) {
            jfloat value = (jfloat) GET_REGISTER_FLOAT(vdst);
            env->SetStaticFloatField(jclass1, jfieldID1, value);
        }
    }
    FINISH(3);
    OP_END

/* File: c/OP_SPUT_WIDE.cpp */
//    HANDLE_SPUT_X(OP_SPUT_WIDE, "-wide", Long, _WIDE)
//        OP_END
    HANDLE_OPCODE(OP_SPUT_WIDE)
    {
        vdst = INST_AA(inst);
        orgref = FETCH(1);
        curref = FETCH(2);
        MY_LOG_VERBOSE("|sput_wide v%d,sfield@0x%04x", vdst, orgref);
        int classnameIdx = ycFile->mYcFormat.mFieldItem[curref].classNameIdx;
        MY_LOG_VERBOSE("|classNameIdx %d", classnameIdx);
        int typenameIdx = ycFile->mYcFormat.mFieldItem[curref].typeIdx;
        MY_LOG_VERBOSE("|typeIdx %d", typenameIdx);
        int fieldnameIdx = ycFile->mYcFormat.mFieldItem[curref].fieldNameIdx;
        MY_LOG_VERBOSE("|fieldNameIdx %d", fieldnameIdx);
        char *classname = ycFile->mYcFormat.mStringItem[classnameIdx];
        MY_LOG_VERBOSE("|className %s", classname);
        char *typeName = ycFile->mYcFormat.mStringItem[typenameIdx];
        MY_LOG_VERBOSE("|typeName %s", typeName);
        char *fieldname = ycFile->mYcFormat.mStringItem[fieldnameIdx];
        MY_LOG_VERBOSE("|fieldName %s", fieldname);
        jclass jclass1 = env->FindClass(classname);
        jfieldID jfieldID1 = env->GetStaticFieldID(jclass1, fieldname, typeName);
        if (strcmp(typeName, "J") == 0) {
            jlong value = (jlong) GET_REGISTER_WIDE(vdst);
            env->SetStaticLongField(jclass1, jfieldID1, value);
        }
        if (strcmp(typeName, "D") == 0) {
            jdouble value = (jdouble) GET_REGISTER_DOUBLE(vdst);
            env->SetStaticDoubleField(jclass1, jfieldID1, value);
        }
    }
    FINISH(3);
    OP_END

/* File: c/OP_SPUT_OBJECT.cpp */
HANDLE_SPUT_X(OP_SPUT_OBJECT, "-object", Object, _AS_OBJECT)
    OP_END


/* File: c/OP_SPUT_BOOLEAN.cpp */
HANDLE_SPUT_X(OP_SPUT_BOOLEAN, "", Int,)
    OP_END

/* File: c/OP_SPUT_BYTE.cpp */
HANDLE_SPUT_X(OP_SPUT_BYTE, "", Int,)
    OP_END

/* File: c/OP_SPUT_CHAR.cpp */
HANDLE_SPUT_X(OP_SPUT_CHAR, "", Int,)
    OP_END

/* File: c/OP_SPUT_SHORT.cpp */
HANDLE_SPUT_X(OP_SPUT_SHORT, "", Int,)
    OP_END




/* File: c/OP_UNUSED_79.cpp */
    HANDLE_OPCODE(OP_UNUSED_79)
    OP_END

/* File: c/OP_UNUSED_7A.cpp */
    HANDLE_OPCODE(OP_UNUSED_7A)
    OP_END

/* File: c/OP_NEG_INT.cpp */
HANDLE_UNOP(OP_NEG_INT, "neg-int", -, ,)
    OP_END

/* File: c/OP_NOT_INT.cpp */
HANDLE_UNOP(OP_NOT_INT, "not-int", , ^
        0xffffffff,)
    OP_END

/* File: c/OP_NEG_LONG.cpp */
HANDLE_UNOP(OP_NEG_LONG, "neg-long", -, , _WIDE)
    OP_END

/* File: c/OP_NOT_LONG.cpp */
HANDLE_UNOP(OP_NOT_LONG, "not-long", , ^
        0xffffffffffffffffULL, _WIDE)
    OP_END

/* File: c/OP_NEG_FLOAT.cpp */
HANDLE_UNOP(OP_NEG_FLOAT, "neg-float", -, , _FLOAT)
    OP_END

/* File: c/OP_NEG_DOUBLE.cpp */
HANDLE_UNOP(OP_NEG_DOUBLE, "neg-double", -, , _DOUBLE)
    OP_END

/* File: c/OP_INT_TO_LONG.cpp */
HANDLE_NUMCONV(OP_INT_TO_LONG, "int-to-long", _INT, _WIDE)
    OP_END

/* File: c/OP_INT_TO_FLOAT.cpp */
HANDLE_NUMCONV(OP_INT_TO_FLOAT, "int-to-float", _INT, _FLOAT)
    OP_END

/* File: c/OP_INT_TO_DOUBLE.cpp */
HANDLE_NUMCONV(OP_INT_TO_DOUBLE, "int-to-double", _INT, _DOUBLE)
    OP_END

/* File: c/OP_LONG_TO_INT.cpp */
HANDLE_NUMCONV(OP_LONG_TO_INT, "long-to-int", _WIDE, _INT)
    OP_END

/* File: c/OP_LONG_TO_FLOAT.cpp */
HANDLE_NUMCONV(OP_LONG_TO_FLOAT, "long-to-float", _WIDE, _FLOAT)
    OP_END

/* File: c/OP_LONG_TO_DOUBLE.cpp */
HANDLE_NUMCONV(OP_LONG_TO_DOUBLE, "long-to-double", _WIDE, _DOUBLE)
    OP_END

/* File: c/OP_FLOAT_TO_INT.cpp */
HANDLE_FLOAT_TO_INT(OP_FLOAT_TO_INT, "float-to-int",
                    float, _FLOAT, s4, _INT)
    OP_END

/* File: c/OP_FLOAT_TO_LONG.cpp */
HANDLE_FLOAT_TO_INT(OP_FLOAT_TO_LONG, "float-to-long",
                    float, _FLOAT, s8, _WIDE)
    OP_END

/* File: c/OP_FLOAT_TO_DOUBLE.cpp */
HANDLE_NUMCONV(OP_FLOAT_TO_DOUBLE, "float-to-double", _FLOAT, _DOUBLE)
    OP_END

/* File: c/OP_DOUBLE_TO_INT.cpp */
HANDLE_FLOAT_TO_INT(OP_DOUBLE_TO_INT, "double-to-int",
                    double, _DOUBLE, s4, _INT)
    OP_END

/* File: c/OP_DOUBLE_TO_LONG.cpp */
HANDLE_FLOAT_TO_INT(OP_DOUBLE_TO_LONG, "double-to-long",
                    double, _DOUBLE, s8, _WIDE)
    OP_END

HANDLE_NUMCONV(OP_DOUBLE_TO_FLOAT, "double-to-float", _DOUBLE, _FLOAT)
    OP_END
/* File: c/OP_INT_TO_BYTE.cpp */
HANDLE_INT_TO_SMALL(OP_INT_TO_BYTE, "byte", s1)
    OP_END

/* File: c/OP_INT_TO_CHAR.cpp */
HANDLE_INT_TO_SMALL(OP_INT_TO_CHAR, "char", u2)
    OP_END

/* File: c/OP_INT_TO_SHORT.cpp */
HANDLE_INT_TO_SMALL(OP_INT_TO_SHORT, "short", s2)    /* want sign bit */
    OP_END

/* File: c/OP_ADD_INT.cpp */
HANDLE_OP_X_INT(OP_ADD_INT, "add", +, 0)
    OP_END

/* File: c/OP_SUB_INT.cpp */
HANDLE_OP_X_INT(OP_SUB_INT, "sub", -, 0)
    OP_END

/* File: c/OP_MUL_INT.cpp */
HANDLE_OP_X_INT(OP_MUL_INT, "mul", *, 0)
    OP_END

/* File: c/OP_DIV_INT.cpp */
HANDLE_OP_X_INT(OP_DIV_INT, "div", /, 1)
    OP_END

/* File: c/OP_REM_INT.cpp */
HANDLE_OP_X_INT(OP_REM_INT, "rem", %, 2)
    OP_END

/* File: c/OP_AND_INT.cpp */
HANDLE_OP_X_INT(OP_AND_INT, "and", &, 0)
    OP_END

/* File: c/OP_OR_INT.cpp */
HANDLE_OP_X_INT(OP_OR_INT, "or", |, 0)
    OP_END

/* File: c/OP_XOR_INT.cpp */
HANDLE_OP_X_INT(OP_XOR_INT, "xor", ^, 0)
    OP_END
/* File: c/OP_SHL_INT.cpp */
HANDLE_OP_SHX_INT(OP_SHL_INT, "shl", (s4), <<)
    OP_END

/* File: c/OP_SHR_INT.cpp */
HANDLE_OP_SHX_INT(OP_SHR_INT, "shr", (s4), >>)
    OP_END

/* File: c/OP_USHR_INT.cpp */
HANDLE_OP_SHX_INT(OP_USHR_INT, "ushr", (u4), >>)
    OP_END

/* File: c/OP_ADD_LONG.cpp */
HANDLE_OP_X_LONG(OP_ADD_LONG, "add", +, 0)
    OP_END

/* File: c/OP_SUB_LONG.cpp */
HANDLE_OP_X_LONG(OP_SUB_LONG, "sub", -, 0)
    OP_END

/* File: c/OP_MUL_LONG.cpp */
HANDLE_OP_X_LONG(OP_MUL_LONG, "mul", *, 0)
    OP_END

/* File: c/OP_DIV_LONG.cpp */
HANDLE_OP_X_LONG(OP_DIV_LONG, "div", /, 1)
    OP_END

/* File: c/OP_REM_LONG.cpp */
HANDLE_OP_X_LONG(OP_REM_LONG, "rem", %, 2)
    OP_END

/* File: c/OP_AND_LONG.cpp */
HANDLE_OP_X_LONG(OP_AND_LONG, "and", &, 0)
    OP_END

/* File: c/OP_OR_LONG.cpp */
HANDLE_OP_X_LONG(OP_OR_LONG, "or", |, 0)
    OP_END

/* File: c/OP_XOR_LONG.cpp */
HANDLE_OP_X_LONG(OP_XOR_LONG, "xor", ^, 0)
    OP_END

/* File: c/OP_SHL_LONG.cpp */
HANDLE_OP_SHX_LONG(OP_SHL_LONG, "shl", (s8), <<)
    OP_END

/* File: c/OP_SHR_LONG.cpp */
HANDLE_OP_SHX_LONG(OP_SHR_LONG, "shr", (s8), >>)
    OP_END

/* File: c/OP_USHR_LONG.cpp */
HANDLE_OP_SHX_LONG(OP_USHR_LONG, "ushr", (u8), >>)
    OP_END

/* File: c/OP_ADD_FLOAT.cpp */
HANDLE_OP_X_FLOAT(OP_ADD_FLOAT, "add", +)
    OP_END

/* File: c/OP_SUB_FLOAT.cpp */
HANDLE_OP_X_FLOAT(OP_SUB_FLOAT, "sub", -)
    OP_END

/* File: c/OP_MUL_FLOAT.cpp */
HANDLE_OP_X_FLOAT(OP_MUL_FLOAT, "mul", *)
    OP_END

/* File: c/OP_DIV_FLOAT.cpp */
HANDLE_OP_X_FLOAT(OP_DIV_FLOAT, "div", /)
    OP_END

    HANDLE_OPCODE(OP_REM_FLOAT)
    {
        u2 srcRegs;
        vdst = INST_AA(inst);
        srcRegs = FETCH(1);
        vsrc1 = srcRegs & 0xff;
        vsrc2 = srcRegs >> 8;
        MY_LOG_VERBOSE("|%s-float v%d,v%d,v%d", "mod", vdst, vsrc1, vsrc2);
        SET_REGISTER_FLOAT(vdst,
                           fmodf(GET_REGISTER_FLOAT(vsrc1), GET_REGISTER_FLOAT(vsrc2)));
    }
    FINISH(2);
    OP_END


/* File: c/OP_ADD_DOUBLE.cpp */
HANDLE_OP_X_DOUBLE(OP_ADD_DOUBLE, "add", +)
    OP_END

/* File: c/OP_SUB_DOUBLE.cpp */
HANDLE_OP_X_DOUBLE(OP_SUB_DOUBLE, "sub", -)
    OP_END

/* File: c/OP_MUL_DOUBLE.cpp */
HANDLE_OP_X_DOUBLE(OP_MUL_DOUBLE, "mul", *)
    OP_END

/* File: c/OP_DIV_DOUBLE.cpp */
HANDLE_OP_X_DOUBLE(OP_DIV_DOUBLE, "div", /)
    OP_END
    HANDLE_OPCODE(OP_REM_DOUBLE)
/* File: c/OP_ADD_INT_2ADDR.cpp */
HANDLE_OP_X_INT_2ADDR(OP_ADD_INT_2ADDR, "add", +, 0)
    OP_END

/* File: c/OP_SUB_INT_2ADDR.cpp */
HANDLE_OP_X_INT_2ADDR(OP_SUB_INT_2ADDR, "sub", -, 0)
    OP_END

/* File: c/OP_MUL_INT_2ADDR.cpp */
HANDLE_OP_X_INT_2ADDR(OP_MUL_INT_2ADDR, "mul", *, 0)
    OP_END

/* File: c/OP_DIV_INT_2ADDR.cpp */
HANDLE_OP_X_INT_2ADDR(OP_DIV_INT_2ADDR, "div", /, 1)
    OP_END

/* File: c/OP_REM_INT_2ADDR.cpp */
HANDLE_OP_X_INT_2ADDR(OP_REM_INT_2ADDR, "rem", %, 2)
    OP_END

/* File: c/OP_AND_INT_2ADDR.cpp */
HANDLE_OP_X_INT_2ADDR(OP_AND_INT_2ADDR, "and", &, 0)
    OP_END

/* File: c/OP_OR_INT_2ADDR.cpp */
HANDLE_OP_X_INT_2ADDR(OP_OR_INT_2ADDR, "or", |, 0)
    OP_END

/* File: c/OP_XOR_INT_2ADDR.cpp */
HANDLE_OP_X_INT_2ADDR(OP_XOR_INT_2ADDR, "xor", ^, 0)
    OP_END

/* File: c/OP_SHL_INT_2ADDR.cpp */
HANDLE_OP_SHX_INT_2ADDR(OP_SHL_INT_2ADDR, "shl", (s4), <<)
    OP_END

/* File: c/OP_SHR_INT_2ADDR.cpp */
HANDLE_OP_SHX_INT_2ADDR(OP_SHR_INT_2ADDR, "shr", (s4), >>)
    OP_END

/* File: c/OP_USHR_INT_2ADDR.cpp */
HANDLE_OP_SHX_INT_2ADDR(OP_USHR_INT_2ADDR, "ushr", (u4), >>)
    OP_END

/* File: c/OP_ADD_LONG_2ADDR.cpp */
HANDLE_OP_X_LONG_2ADDR(OP_ADD_LONG_2ADDR, "add", +, 0)
    OP_END

/* File: c/OP_SUB_LONG_2ADDR.cpp */
HANDLE_OP_X_LONG_2ADDR(OP_SUB_LONG_2ADDR, "sub", -, 0)
    OP_END

/* File: c/OP_MUL_LONG_2ADDR.cpp */
HANDLE_OP_X_LONG_2ADDR(OP_MUL_LONG_2ADDR, "mul", *, 0)
    OP_END

/* File: c/OP_DIV_LONG_2ADDR.cpp */
HANDLE_OP_X_LONG_2ADDR(OP_DIV_LONG_2ADDR, "div", /, 1)
    OP_END

/* File: c/OP_REM_LONG_2ADDR.cpp */
HANDLE_OP_X_LONG_2ADDR(OP_REM_LONG_2ADDR, "rem", %, 2)
    OP_END

/* File: c/OP_AND_LONG_2ADDR.cpp */
HANDLE_OP_X_LONG_2ADDR(OP_AND_LONG_2ADDR, "and", &, 0)
    OP_END

/* File: c/OP_OR_LONG_2ADDR.cpp */
HANDLE_OP_X_LONG_2ADDR(OP_OR_LONG_2ADDR, "or", |, 0)
    OP_END

/* File: c/OP_XOR_LONG_2ADDR.cpp */
HANDLE_OP_X_LONG_2ADDR(OP_XOR_LONG_2ADDR, "xor", ^, 0)
    OP_END

/* File: c/OP_SHL_LONG_2ADDR.cpp */
HANDLE_OP_SHX_LONG_2ADDR(OP_SHL_LONG_2ADDR, "shl", (s8), <<)
    OP_END

/* File: c/OP_SHR_LONG_2ADDR.cpp */
HANDLE_OP_SHX_LONG_2ADDR(OP_SHR_LONG_2ADDR, "shr", (s8), >>)
    OP_END

/* File: c/OP_USHR_LONG_2ADDR.cpp */
HANDLE_OP_SHX_LONG_2ADDR(OP_USHR_LONG_2ADDR, "ushr", (u8), >>)
    OP_END

/* File: c/OP_ADD_FLOAT_2ADDR.cpp */
HANDLE_OP_X_FLOAT_2ADDR(OP_ADD_FLOAT_2ADDR, "add", +)
    OP_END

/* File: c/OP_SUB_FLOAT_2ADDR.cpp */
HANDLE_OP_X_FLOAT_2ADDR(OP_SUB_FLOAT_2ADDR, "sub", -)
    OP_END

/* File: c/OP_MUL_FLOAT_2ADDR.cpp */
HANDLE_OP_X_FLOAT_2ADDR(OP_MUL_FLOAT_2ADDR, "mul", *)
    OP_END

/* File: c/OP_DIV_FLOAT_2ADDR.cpp */
HANDLE_OP_X_FLOAT_2ADDR(OP_DIV_FLOAT_2ADDR, "div", /)
    OP_END

    HANDLE_OPCODE(OP_REM_FLOAT_2ADDR)
    vdst = INST_A(inst);
    vsrc1 = INST_B(inst);
    MY_LOG_VERBOSE("|%s-float-2addr v%d,v%d", "mod", vdst, vsrc1);
    SET_REGISTER_FLOAT(vdst, fmodf(GET_REGISTER_FLOAT(vdst), GET_REGISTER_FLOAT(vsrc1)));
    FINISH(1);
    OP_END

/* File: c/OP_ADD_DOUBLE_2ADDR.cpp */
HANDLE_OP_X_DOUBLE_2ADDR(OP_ADD_DOUBLE_2ADDR, "add", +)
    OP_END

/* File: c/OP_SUB_DOUBLE_2ADDR.cpp */
HANDLE_OP_X_DOUBLE_2ADDR(OP_SUB_DOUBLE_2ADDR, "sub", -)
    OP_END

/* File: c/OP_MUL_DOUBLE_2ADDR.cpp */
HANDLE_OP_X_DOUBLE_2ADDR(OP_MUL_DOUBLE_2ADDR, "mul", *)
    OP_END

/* File: c/OP_DIV_DOUBLE_2ADDR.cpp */
HANDLE_OP_X_DOUBLE_2ADDR(OP_DIV_DOUBLE_2ADDR, "div", /)
    OP_END

    HANDLE_OPCODE(OP_REM_DOUBLE_2ADDR)
/* File: c/OP_ADD_INT_LIT16.cpp */
HANDLE_OP_X_INT_LIT16(OP_ADD_INT_LIT16, "add", +, 0)
    OP_END

/* File: c/OP_RSUB_INT.cpp */
    HANDLE_OPCODE(OP_RSUB_INT /*vA, vB, #+CCCC*/)
    {
        vdst = INST_A(inst);
        vsrc1 = INST_B(inst);
        vsrc2 = FETCH(1);
        MY_LOG_VERBOSE("|rsub-int v%d,v%d,#+0x%04x", vdst, vsrc1, vsrc2);
        SET_REGISTER(vdst, (s2) vsrc2 - (s4) GET_REGISTER(vsrc1));
    }
    FINISH(2);
    OP_END

/* File: c/OP_MUL_INT_LIT16.cpp */
HANDLE_OP_X_INT_LIT16(OP_MUL_INT_LIT16, "mul", *, 0)
    OP_END

/* File: c/OP_DIV_INT_LIT16.cpp */
HANDLE_OP_X_INT_LIT16(OP_DIV_INT_LIT16, "div", /, 1)
    OP_END

/* File: c/OP_REM_INT_LIT16.cpp */
HANDLE_OP_X_INT_LIT16(OP_REM_INT_LIT16, "rem", %, 2)
    OP_END

/* File: c/OP_AND_INT_LIT16.cpp */
HANDLE_OP_X_INT_LIT16(OP_AND_INT_LIT16, "and", &, 0)
    OP_END

/* File: c/OP_OR_INT_LIT16.cpp */
HANDLE_OP_X_INT_LIT16(OP_OR_INT_LIT16, "or", |, 0)
    OP_END

/* File: c/OP_XOR_INT_LIT16.cpp */
HANDLE_OP_X_INT_LIT16(OP_XOR_INT_LIT16, "xor", ^, 0)
    OP_END

/* File: c/OP_ADD_INT_LIT8.cpp */
HANDLE_OP_X_INT_LIT8(OP_ADD_INT_LIT8, "add", +, 0)
    OP_END

/* File: c/OP_RSUB_INT_LIT8.cpp */
    HANDLE_OPCODE(OP_RSUB_INT_LIT8 /*vAA, vBB, #+CC*/)
    {
        u2 litInfo;
        vdst = INST_AA(inst);
        litInfo = FETCH(1);
        vsrc1 = litInfo & 0xff;
        vsrc2 = litInfo >> 8;
        MY_LOG_VERBOSE("|%s-int/lit8 v%d,v%d,#+0x%02x", "rsub", vdst, vsrc1, vsrc2);
        SET_REGISTER(vdst, (s1) vsrc2 - (s4) GET_REGISTER(vsrc1));
    }
    FINISH(2);
    OP_END



/* File: c/OP_MUL_INT_LIT8.cpp */
HANDLE_OP_X_INT_LIT8(OP_MUL_INT_LIT8, "mul", *, 0)
    OP_END


/* File: c/OP_DIV_INT_LIT8.cpp */
HANDLE_OP_X_INT_LIT8(OP_DIV_INT_LIT8, "div", /, 1)
    OP_END

/* File: c/OP_REM_INT_LIT8.cpp */
HANDLE_OP_X_INT_LIT8(OP_REM_INT_LIT8, "rem", %, 2)
    OP_END

/* File: c/OP_AND_INT_LIT8.cpp */
HANDLE_OP_X_INT_LIT8(OP_AND_INT_LIT8, "and", &, 0)
    OP_END

/* File: c/OP_OR_INT_LIT8.cpp */
HANDLE_OP_X_INT_LIT8(OP_OR_INT_LIT8, "or", |, 0)
    OP_END

/* File: c/OP_XOR_INT_LIT8.cpp */
HANDLE_OP_X_INT_LIT8(OP_XOR_INT_LIT8, "xor", ^, 0)
    OP_END

/* File: c/OP_SHL_INT_LIT8.cpp */
HANDLE_OP_SHX_INT_LIT8(OP_SHL_INT_LIT8, "shl", (s4), <<)
    OP_END

/* File: c/OP_SHR_INT_LIT8.cpp */
HANDLE_OP_SHX_INT_LIT8(OP_SHR_INT_LIT8, "shr", (s4), >>)
    OP_END

/* File: c/OP_USHR_INT_LIT8.cpp */
HANDLE_OP_SHX_INT_LIT8(OP_USHR_INT_LIT8, "ushr", (u4), >>)
    OP_END

//TODO 5
    HANDLE_OPCODE(OP_IGET_VOLATILE)
    HANDLE_OPCODE(OP_IPUT_VOLATILE)
    HANDLE_OPCODE(OP_SGET_VOLATILE)
    HANDLE_OPCODE(OP_SPUT_VOLATILE)
    HANDLE_OPCODE(OP_IGET_OBJECT_VOLATILE)
    HANDLE_OPCODE(OP_IGET_WIDE_VOLATILE)
    HANDLE_OPCODE(OP_IPUT_WIDE_VOLATILE)
    HANDLE_OPCODE(OP_SGET_WIDE_VOLATILE)
    HANDLE_OPCODE(OP_SPUT_WIDE_VOLATILE)


    HANDLE_OPCODE(OP_BREAKPOINT)
    HANDLE_OPCODE(OP_THROW_VERIFICATION_ERROR)
    HANDLE_OPCODE(OP_EXECUTE_INLINE)
    HANDLE_OPCODE(OP_EXECUTE_INLINE_RANGE)
    HANDLE_OPCODE(OP_INVOKE_OBJECT_INIT_RANGE)
    HANDLE_OPCODE(OP_RETURN_VOID_BARRIER)
    HANDLE_OPCODE(OP_IGET_QUICK)
    HANDLE_OPCODE(OP_IGET_WIDE_QUICK)
    HANDLE_OPCODE(OP_IGET_OBJECT_QUICK)
    HANDLE_OPCODE(OP_IPUT_QUICK)
    HANDLE_OPCODE(OP_IPUT_WIDE_QUICK)
    HANDLE_OPCODE(OP_IPUT_OBJECT_QUICK)
    HANDLE_OPCODE(OP_INVOKE_VIRTUAL_QUICK)
    HANDLE_OPCODE(OP_INVOKE_VIRTUAL_QUICK_RANGE)
    HANDLE_OPCODE(OP_INVOKE_SUPER_QUICK)
    HANDLE_OPCODE(OP_INVOKE_SUPER_QUICK_RANGE)
    HANDLE_OPCODE(OP_IPUT_OBJECT_VOLATILE)
    HANDLE_OPCODE(OP_SGET_OBJECT_VOLATILE)
    HANDLE_OPCODE(OP_SPUT_OBJECT_VOLATILE)
    HANDLE_OPCODE(OP_UNUSED_FF)

/*
 * Jump here when the code throws an exception
 */
    GOTO_TARGET(exceptionThrown)
    GOTO_TARGET_END
//�ͷſ���ָ��
    bail:
    if (NULL != params) {
        delete[]
                params;
    }
//    while(--mJstringsize>=0)
//    {
//        env->DeleteLocalRef(mJstring[mJstringsize]);
//    }
//    if(NULL!=mJstring)
//    {
//        free(mJstring);
//    }
//    while (--mClassElemSize>=0)
//    {
//        env->DeleteLocalRef(mClass[mClassElemSize]);
//    }
//    if(NULL!=mClass)
//    {
//        free(mClass);
//    }
//    while (--mObjectsize>=0)
//    {
//        env->DeleteLocalRef(mObject[mObjectsize]);
//    }
//    if(NULL!=mObject)
//    {
//        free(mObject);
//    }
    MY_LOG_INFO("|-- Leaving interpreter loop");
    //return retval;
    return returnval.val;
}





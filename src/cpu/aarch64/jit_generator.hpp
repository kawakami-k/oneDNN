/*******************************************************************************
* Copyright 2016-2021 Intel Corporation
* Copyright 2020-2021 FUJITSU LIMITED
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#ifndef CPU_AARCH64_JIT_GENERATOR_HPP
#define CPU_AARCH64_JIT_GENERATOR_HPP

#include <limits.h>

#include "common/bit_cast.hpp"
#include "common/type_helpers.hpp"
#include "common/utils.hpp"

#include "cpu/aarch64/cpu_isa_traits.hpp"

#include "cpu/aarch64/jit_op_imm_check.hpp"
#include "cpu/aarch64/jit_utils/jit_utils.hpp"

#define STRUCT_ALIGN(al, ...) __VA_ARGS__ __attribute__((__aligned__(al)))

#define DECLARE_CPU_JIT_AUX_FUNCTIONS(jit_name) \
    const char *name() const override { return STRINGIFY(jit_name); } \
    const char *source_file() const override { return __FILE__; }

namespace dnnl {
namespace impl {
namespace cpu {
namespace aarch64 {

// TODO: move this to jit_generator class?
namespace {

typedef enum {
    MAX_CODE_SIZE = 256 * 1024,
} max_code_size_t;

// TODO: move this somewhere else? Although this is only used by jit kernels
// (Roma)
static inline int float2int(float x) {
    return utils::bit_cast<int>(x);
}

// Callee-saved registers
constexpr Xbyak_aarch64::Operand::Code abi_save_gpr_regs[]
        = {Xbyak_aarch64::Operand::X19, Xbyak_aarch64::Operand::X20,
                Xbyak_aarch64::Operand::X21, Xbyak_aarch64::Operand::X22,
                Xbyak_aarch64::Operand::X23, Xbyak_aarch64::Operand::X24,
                Xbyak_aarch64::Operand::X25, Xbyak_aarch64::Operand::X26,
                Xbyak_aarch64::Operand::X27, Xbyak_aarch64::Operand::X28};

// See "Procedure Call Standsard for the ARM 64-bit Architecture (AArch64)"
static const Xbyak_aarch64::XReg abi_param1(Xbyak_aarch64::Operand::X0),
        abi_param2(Xbyak_aarch64::Operand::X1),
        abi_param3(Xbyak_aarch64::Operand::X2),
        abi_param4(Xbyak_aarch64::Operand::X3),
        abi_param5(Xbyak_aarch64::Operand::X4),
        abi_param6(Xbyak_aarch64::Operand::X5),
        abi_param7(Xbyak_aarch64::Operand::X6),
        abi_param8(Xbyak_aarch64::Operand::X7),
        abi_not_param1(Xbyak_aarch64::Operand::X15);

static const Xbyak::Reg64 abi_param1_x64(Xbyak::Operand::RDI),
        abi_param2_x64(Xbyak::Operand::RSI),
        abi_param3_x64(Xbyak::Operand::RDX),
        abi_param4_x64(Xbyak::Operand::RCX), abi_param5_x64(Xbyak::Operand::R8),
        abi_param6_x64(Xbyak::Operand::R9),
        abi_not_param1_x64(Xbyak::Operand::RCX);
} // namespace

class jit_generator : public Xbyak::CodeGenerator, public c_compatible {
public:
    using c_compatible::operator new;
    using c_compatible::operator new[];
    using c_compatible::operator delete;
    using c_compatible::operator delete[];

private:
    const size_t xreg_len = 8;
    const size_t vreg_len_preserve = 8; // Only bottom 8byte must be preserved.
    const size_t vreg_to_preserve = 8; // VREG8 - VREG15

    const size_t num_abi_save_gpr_regs
            = sizeof(abi_save_gpr_regs) / sizeof(abi_save_gpr_regs[0]);

    const size_t preserved_stack_size = xreg_len * (2 + num_abi_save_gpr_regs)
            + vreg_len_preserve * vreg_to_preserve;

    const size_t size_of_abi_save_regs = num_abi_save_gpr_regs * 64 / 8
            + vreg_to_preserve * vreg_len_preserve;

public:
    enum {
        _cmp_eq_oq = 0u,
        _cmp_lt_os = 1u,
        _cmp_le_os = 2u,
        _cmp_neq_uq = 4u,
        _cmp_nlt_us = 5u,
        _cmp_nle_us = 6u,

        _op_floor = 1u,
        _op_mxcsr = 4u,
    };

    const Xbyak_aarch64::WReg W_TMP_0 {23};
    const Xbyak_aarch64::WReg W_TMP_1 {24};
    const Xbyak_aarch64::WReg W_TMP_2 {25};
    const Xbyak_aarch64::WReg W_TMP_3 {26};
    const Xbyak_aarch64::WReg W_TMP_4 {27};
    const Xbyak_aarch64::XReg X_TMP_0 {23};
    const Xbyak_aarch64::XReg X_TMP_1 {24};
    const Xbyak_aarch64::XReg X_TMP_2 {25};
    const Xbyak_aarch64::XReg X_TMP_3 {26};
    const Xbyak_aarch64::XReg X_TMP_4 {27};
    const Xbyak_aarch64::XReg X_DEFAULT_ADDR {28};
    const Xbyak_aarch64::XReg X_SP {21};
    const Xbyak_aarch64::XReg X_TRANSLATOR_STACK {22};
    const Xbyak_aarch64::PReg P_TMP {0};
    const Xbyak_aarch64::PReg P_TMP_0 {11};
    const Xbyak_aarch64::PReg P_TMP_1 {12};
    const Xbyak_aarch64::PReg P_ALL_ZERO {10};
    const Xbyak_aarch64::PReg P_MSB_256 {13};
    const Xbyak_aarch64::PReg P_MSB_384 {14};
    const Xbyak_aarch64::PReg P_ALL_ONE {15};

    const std::vector<Xbyak_aarch64::XReg> x_tmp_vec
            = {X_TMP_0, X_TMP_1, X_TMP_2, X_TMP_3, X_TMP_4};
    const int x_tmp_vec_size = x_tmp_vec.size();

    const Xbyak_aarch64::XReg param1 = abi_param1;
    const Xbyak::Reg64 param1_x64 = abi_param1_x64;
    constexpr static size_t translator_stack_offset = 1024 * 128;
    constexpr static uint32_t DUMMY_IDX = 99;

    const int EVEX_max_8b_offt = 0x200;
    const Xbyak::Reg64 reg_EVEX_max_8b_offt = rbp;

    inline size_t get_size_of_abi_save_regs() { return size_of_abi_save_regs; }

    void preamble(bool isDirect = false) {
        xa_->stp(x29, x30, pre_ptr(xa_->sp, -16));
        /* x29 is a frame pointer. */
        xa_->mov(x29, xa_->sp);
        xa_->sub(xa_->sp, xa_->sp,
                static_cast<int64_t>(preserved_stack_size) - 16);

        /* x9 can be used as a temporal register. */
        xa_->mov(x9, xa_->sp);

        if (vreg_to_preserve) {
            xa_->st4((v8.d - v11.d)[0], post_ptr(x9, vreg_len_preserve * 4));
            xa_->st4((v12.d - v15.d)[0], post_ptr(x9, vreg_len_preserve * 4));
        }
        for (size_t i = 0; i < num_abi_save_gpr_regs; i += 2) {
            xa_->stp(Xbyak_aarch64::XReg(abi_save_gpr_regs[i]),
                    Xbyak_aarch64::XReg(abi_save_gpr_regs[i + 1]),
                    post_ptr(x9, xreg_len * 2));
        }

        if (mayiuse(sve_512) || mayiuse(sve_256)) {
            xa_->ptrue(P_ALL_ONE.b);
            xa_->ptrue(P_MSB_384.b, Xbyak_aarch64::VL16);
            xa_->ptrue(P_MSB_256.b, Xbyak_aarch64::VL32);
            xa_->not_(P_MSB_384.b, P_ALL_ONE / Xbyak_aarch64::T_z, P_MSB_384.b);
            xa_->not_(P_MSB_256.b, P_ALL_ONE / Xbyak_aarch64::T_z, P_MSB_256.b);
            xa_->pfalse(P_ALL_ZERO.b);
        }

        /* arg values are passed different registers between x86_64 and aarch64. */
        /* Note:If # of args is more than 6, 7-th, 8-th, ..., args are passed by stack. */
        if (isDirect == false) {
            xa_->mov(x7, x0); /* First arg. */
            xa_->mov(x6, x1); /* Sedond arg. */
            xa_->mov(x2, x2);
            xa_->mov(x1, x3);
            xa_->mov(x8, x4);
            xa_->mov(x9, x5); /* 6-th arg. */

            if (mayiuse(avx512_common)) {
                mov(reg_EVEX_max_8b_offt, 2 * EVEX_max_8b_offt);
            }
        }

        xa_->mov(X_SP, xa_->sp);
        xa_->sub_imm(
                X_TRANSLATOR_STACK, X_SP, translator_stack_offset, X_TMP_0);
        xa_->mov_imm(X_TMP_0,
                getTranslatorVersion()); /*get translator version info */
    }

    void postamble() {
        xa_->mov(x9, xa_->sp);
        if (mayiuse(sve_512) || mayiuse(sve_256)) {
            xa_->eor(P_ALL_ONE.b, P_ALL_ONE / Xbyak_aarch64::T_z, P_ALL_ONE.b,
                    P_ALL_ONE.b);
            xa_->eor(P_MSB_384.b, P_MSB_384 / Xbyak_aarch64::T_z, P_MSB_384.b,
                    P_MSB_384.b);
            xa_->eor(P_MSB_256.b, P_MSB_256 / Xbyak_aarch64::T_z, P_MSB_256.b,
                    P_MSB_256.b);
        }

        if (vreg_to_preserve) {
            xa_->ld4((v8.d - v11.d)[0], post_ptr(x9, vreg_len_preserve * 4));
            xa_->ld4((v12.d - v15.d)[0], post_ptr(x9, vreg_len_preserve * 4));
        }

        for (size_t i = 0; i < num_abi_save_gpr_regs; i += 2) {
            xa_->ldp(Xbyak_aarch64::XReg(abi_save_gpr_regs[i]),
                    Xbyak_aarch64::XReg(abi_save_gpr_regs[i + 1]),
                    post_ptr(x9, xreg_len * 2));
        }

        xa_->add(xa_->sp, xa_->sp,
                static_cast<int64_t>(preserved_stack_size) - 16);
        xa_->ldp(x29, x30, Xbyak_aarch64::post_ptr(xa_->sp, 16));
        xa_->ret();
    }

    template <typename T>
    Xbyak::Address EVEX_compress_addr(
            Xbyak::Reg64 base, T raw_offt, bool bcast = false) {
        using Xbyak::Address;
        using Xbyak::Reg64;
        using Xbyak::RegExp;
        using Xbyak::Zmm;

        assert(raw_offt <= INT_MAX);
        auto offt = static_cast<int>(raw_offt);

        int scale = 0;

        if (EVEX_max_8b_offt <= offt && offt < 3 * EVEX_max_8b_offt) {
            offt = offt - 2 * EVEX_max_8b_offt;
            scale = 1;
        } else if (3 * EVEX_max_8b_offt <= offt
                && offt < 5 * EVEX_max_8b_offt) {
            offt = offt - 4 * EVEX_max_8b_offt;
            scale = 2;
        }

        auto re = RegExp() + base + offt;
        if (scale) re = re + reg_EVEX_max_8b_offt * scale;

        if (bcast)
            return zword_b[re];
        else
            return zword[re];
    }

    Xbyak::Address make_safe_addr(const Xbyak::Reg64 &reg_out, size_t offt,
            const Xbyak::Reg64 &tmp_reg, bool bcast = false) {
        if (offt > INT_MAX) {
            mov(tmp_reg, offt);
            return bcast ? ptr_b[reg_out + tmp_reg] : ptr[reg_out + tmp_reg];
        } else {
            return bcast ? ptr_b[reg_out + offt] : ptr[reg_out + offt];
        }
    }

    Xbyak::Address EVEX_compress_addr_safe(const Xbyak::Reg64 &base,
            size_t raw_offt, const Xbyak::Reg64 &reg_offt, bool bcast = false) {
        if (raw_offt > INT_MAX) {
            return make_safe_addr(base, raw_offt, reg_offt, bcast);
        } else {
            return EVEX_compress_addr(base, raw_offt, bcast);
        }
    }

    void safe_add(const Xbyak::Reg64 &base, size_t raw_offt,
            const Xbyak::Reg64 &reg_offt) {
        if (raw_offt > INT_MAX) {
            mov(reg_offt, raw_offt);
            add(base, reg_offt);
        } else {
            add(base, raw_offt);
        }
    }

    void safe_sub(const Xbyak::Reg64 &base, size_t raw_offt,
            const Xbyak::Reg64 &reg_offt) {
        if (raw_offt > INT_MAX) {
            mov(reg_offt, raw_offt);
            sub(base, reg_offt);
        } else {
            sub(base, raw_offt);
        }
    }

    int get_offset(int raw_offt) {

        assert(raw_offt <= INT_MAX);
        auto offt = static_cast<int>(raw_offt);

        int scale = 0;
        const int EVEX_max_8b_offt = 0x200;

        if (EVEX_max_8b_offt <= offt && offt < 3 * EVEX_max_8b_offt) {
            offt = offt - 2 * EVEX_max_8b_offt;
            scale = 1;
        } else if (3 * EVEX_max_8b_offt <= offt
                && offt < 5 * EVEX_max_8b_offt) {
            offt = offt - 4 * EVEX_max_8b_offt;
            scale = 2;
        }

        auto re = offt;
        if (scale) re = re + (2 * EVEX_max_8b_offt) * scale;

        return re;
    }

    Xbyak_aarch64::XReg get_comp_addr_reg(const Xbyak_aarch64::XReg &base,
            const Xbyak_aarch64::XReg &tmp0, const Xbyak_aarch64::XReg &tmp1,
            const int offset) {
        auto offt = get_offset(offset);

        if (offt == 0) return base;
        add_imm(tmp0, base, offt, tmp1);

        return tmp0;
    }

    void ldr_offt(const Xbyak_aarch64::XReg &src,
            const Xbyak_aarch64::XReg &addr, const Xbyak_aarch64::XReg &tmp0,
            const Xbyak_aarch64::XReg &tmp1, const int offt = 0) {
        if (ldr_imm_check(offt))
            ldr(src, Xbyak_aarch64::ptr(addr, offt));
        else
            ldr(src,
                    Xbyak_aarch64::ptr(
                            get_comp_addr_reg(addr, tmp0, tmp1, offt)));
    }

    void str_offt(const Xbyak_aarch64::XReg &src,
            const Xbyak_aarch64::XReg &addr, const Xbyak_aarch64::XReg &tmp0,
            const Xbyak_aarch64::XReg &tmp1, const int offt = 0) {
        if (str_imm_check(offt))
            str(src, Xbyak_aarch64::ptr(addr, offt));
        else
            str(src,
                    Xbyak_aarch64::ptr(
                            get_comp_addr_reg(addr, tmp0, tmp1, offt)));
    }

    // Disallow char-based labels completely
    void L(const char *label) = delete;
    void L(Xbyak_aarch64::Label &label) {
        Xbyak_aarch64::CodeGenerator::L(label);
    }

    void L_aligned(Xbyak_aarch64::Label &label, int alignment = 16) {
        align(alignment);
        L(label);
    }

    void uni_vpxor(const Xbyak::Xmm &x1, const Xbyak::Xmm &x2,
            const Xbyak::Operand &op) {
        if (is_valid_isa(avx512_core))
            vpxord(x1, x2, op);
        else if (is_valid_isa(avx))
            vpxor(x1, x2, op);
        else {
            assert(x1.isEqualIfNotInherited(x2));
            pxor(x2, op);
        }
    }
    void uni_vpxor(const Xbyak::Ymm &x1, const Xbyak::Ymm &x2,
            const Xbyak::Operand &op) {
        if (is_valid_isa(avx512_core))
            vpxord(x1, x2, op);
        else if (is_valid_isa(avx2))
            vpxor(x1, x2, op);
        else
            vxorps(x1, x2, op);
    }
    void uni_vpxor(const Xbyak::Zmm &x1, const Xbyak::Zmm &x2,
            const Xbyak::Operand &op) {
        vpxord(x1, x2, op);
    }

    void uni_vmovss(const Xbyak::Address &addr, const Xbyak::Xmm &x) {
        if (is_valid_isa(avx))
            vmovss(addr, x);
        else
            movss(addr, x);
    }
    void uni_vmovss(const Xbyak::Xmm &x, const Xbyak::Address &addr) {
        if (is_valid_isa(avx))
            vmovss(x, addr);
        else
            movss(x, addr);
    }
    void uni_vmovss(const Xbyak::Xmm &x1, const Xbyak::Xmm &x2) {
        if (is_valid_isa(avx))
            vmovss(x1, x1, x2);
        else
            movss(x1, x2);
    }
    void uni_vmovss(const Xbyak::Address &addr, const Xbyak::Ymm &x) {
        vmovss(addr, Xbyak::Xmm(x.getIdx()));
    }
    void uni_vmovss(const Xbyak::Ymm &x, const Xbyak::Address &addr) {
        vmovss(Xbyak::Xmm(x.getIdx()), addr);
    }
    void uni_vmovss(const Xbyak::Ymm &x1, const Xbyak::Ymm &x2) {
        vmovss(Xbyak::Xmm(x1.getIdx()), Xbyak::Xmm(x2.getIdx()));
    }

    void uni_vmovsd(const Xbyak::Address &addr, const Xbyak::Xmm &x) {
        movsd(addr, x);
    }
    void uni_vmovsd(const Xbyak::Address &addr, const Xbyak::Ymm &x) {
        vmovsd(addr, x);
    }
    void uni_vmovsd(const Xbyak::Xmm &x, const Xbyak::Address &addr) {
        movsd(x, addr);
    }
    void uni_vmovsd(const Xbyak::Ymm &x, const Xbyak::Address &addr) {
        vmovsd(x, addr);
    }

    void uni_vmovdqu(const Xbyak::Address &addr, const Xbyak::Xmm &x) {
        if (is_valid_isa(avx))
            vmovdqu(addr, x);
        else
            movdqu(addr, x);
    }
    void uni_vmovdqu(const Xbyak::Address &addr, const Xbyak::Ymm &x) {
        vmovdqu(addr, x);
    }
    void uni_vmovdqu(const Xbyak::Address &addr, const Xbyak::Zmm &x) {
        vmovdqu32(addr, x);
    }

    void uni_vmovdqu(const Xbyak::Xmm &x, const Xbyak::Address &addr) {
        if (is_valid_isa(avx))
            vmovdqu(x, addr);
        else
            movdqu(x, addr);
    }
    void uni_vmovdqu(const Xbyak::Ymm &x, const Xbyak::Address &addr) {
        vmovdqu(x, addr);
    }
    void uni_vmovdqu(const Xbyak::Zmm &x, const Xbyak::Address &addr) {
        vmovdqu32(x, addr);
    }

    void uni_vmovups(const Xbyak::Address &addr, const Xbyak::Xmm &x) {
        movups(addr, x);
    }
    void uni_vmovups(const Xbyak::Address &addr, const Xbyak::Ymm &x) {
        vmovups(addr, x);
    }

    void uni_vmovups(const Xbyak::Xmm &x, const Xbyak::Operand &op) {
        movups(x, op);
    }
    void uni_vmovups(const Xbyak::Ymm &x, const Xbyak::Operand &op) {
        vmovups(x, op);
    }

    void uni_vmovups_tail(const Xbyak::Address &addr, const Xbyak::Ymm &mask,
            const Xbyak::Ymm &x) {
        vmaskmovps(addr, mask, x);
    }
    void uni_vmovups_tail(const Xbyak::Ymm &x, const Xbyak::Ymm &mask,
            const Xbyak::Address &addr) {
        vmaskmovps(x, mask, addr);
    }

    void uni_vmovups_tail(const Xbyak::Address &addr, const Xbyak::Opmask &mask,
            const Xbyak::Zmm &x) {
        vmovups(addr | mask, x);
    }
    void uni_vmovups_tail(const Xbyak::Zmm &x, const Xbyak::Opmask &mask,
            const Xbyak::Address &addr) {
        vmovups(x | mask | T_z, addr);
    }

    void uni_vmovntps(const Xbyak::Address &addr, const Xbyak::Xmm &x) {
        movntps(addr, x);
    }
    void uni_vmovntps(const Xbyak::Address &addr, const Xbyak::Ymm &x) {
        vmovntps(addr, x);
    }

    void uni_vbroadcastss(const Xbyak::Xmm &x, const Xbyak::Operand &op) {
        movss(x, op);
        shufps(x, x, 0x0);
    }
    void uni_vbroadcastss(const Xbyak::Ymm &x, const Xbyak::Operand &op) {
        if (op.isMEM() || is_valid_isa(avx2)) {
            vbroadcastss(x, op);
        } else {
            Xbyak::Xmm t(x.getIdx());
            if (!t.isEqualIfNotInherited(op)) movss(t, op);
            vinsertf128(x, x, t, 1);
            vshufps(x, x, x, 0);
        }
    }

    void uni_vpbroadcastd(const Xbyak::Xmm &x, const Xbyak::Operand &op) {
        movss(x, op);
        pshufd(x, x, 0x0);
    }
    void uni_vpbroadcastd(const Xbyak::Ymm &x, const Xbyak::Operand &op) {
        if (is_valid_isa(avx2)) {
            vpbroadcastd(x, op);
        } else {
            const Xbyak::Xmm t(x.getIdx());
            if (!t.isEqualIfNotInherited(op)) {
                if (op.isMEM())
                    vmovss(t, op.getAddress());
                else
                    vmovss(t, t, op);
            }
            vinsertf128(x, x, t, 1);
            vshufps(x, x, x, 0);
        }
    }

    void uni_vshufps(const Xbyak::Xmm &x1, const Xbyak::Xmm &x2,
            const Xbyak::Operand &op, Xbyak::uint8 imm) {
        if (is_valid_isa(avx))
            vshufps(x1, x2, op, imm);
        else {
            movups(x1, x2);
            shufps(x1, op, imm);
        }
    }

    void uni_vrcpss(const Xbyak::Xmm &x, const Xbyak::Operand &op) {
        rcpss(x, op);
    }
    void uni_vrcpss(const Xbyak::Ymm &x1, const Xbyak::Xmm &x2) {
        Xbyak::Xmm x1_(x1.getIdx());
        Xbyak::Xmm x2_(x2.getIdx());
        vrcpss(x1_, x1_, x2_);
    }
    void uni_vrcpss(const Xbyak::Ymm &x, const Xbyak::Address &op) {
        Xbyak::Xmm x_(x.getIdx());
        vrcpss(x_, x_, op);
    }

    void uni_vrcpps(const Xbyak::Xmm &x, const Xbyak::Operand &op) {
        rcpps(x, op);
    }
    void uni_vrcpps(const Xbyak::Ymm &x, const Xbyak::Operand &op) {
        vrcpps(x, op);
    }
    void uni_vrcpps(const Xbyak::Zmm &x, const Xbyak::Operand &op) {
        vrcp14ps(x, op);
    }

    void uni_vdivps(const Xbyak::Xmm &x, const Xbyak::Operand &op1,
            const Xbyak::Operand &op2) {
        assert(x.isEqualIfNotInherited(op1));
        divps(x, op2);
    }
    void uni_vdivps(const Xbyak::Ymm &x, const Xbyak::Operand &op1,
            const Xbyak::Operand &op2) {
        vdivps(x, op1, op2);
    }

    void uni_vdivps(const Xbyak::Xmm &x, const Xbyak::Operand &op1,
            const Xbyak::Operand &op2, const Xbyak::Xmm &buf) {
        movups(buf, op1);
        divps(buf, op2);
        if (x.getIdx() != buf.getIdx()) { movups(x, buf); }
    }

    void uni_vdivps(const Xbyak::Ymm &x, const Xbyak::Operand &op1,
            const Xbyak::Operand &op2, const Xbyak::Ymm &buf) {
        vdivps(x, op1, op2);
    }

    void uni_vaddps(const Xbyak::Xmm &x, const Xbyak::Operand &op1,
            const Xbyak::Operand &op2) {
        assert(x.getIdx() == op1.getIdx());
        addps(x, op2);
    }
    void uni_vaddps(const Xbyak::Ymm &x, const Xbyak::Operand &op1,
            const Xbyak::Operand &op2) {
        vaddps(x, op1, op2);
    }
    void uni_vaddss(const Xbyak::Xmm &x, const Xbyak::Operand &op1,
            const Xbyak::Operand &op2) {
        assert(x.isEqualIfNotInherited(op1));
        addss(x, op2);
    }
    void uni_vaddss(const Xbyak::Ymm &x, const Xbyak::Operand &op1,
            const Xbyak::Operand &op2) {
        vaddss(x, op1, op2);
    }

    void uni_vpsignd(const Xbyak::Xmm &x1, const Xbyak::Xmm &x2,
            const Xbyak::Operand &op) {
        assert(x1.getIdx() == x2.getIdx());
        psignd(x1, op);
    }
    void uni_vpsignd(const Xbyak::Ymm &x1, const Xbyak::Ymm &x2,
            const Xbyak::Operand &op) {
        vpsignd(x1, x2, op);
    }

    void uni_vpsubd(const Xbyak::Xmm &x1, const Xbyak::Xmm &x2,
            const Xbyak::Operand &op) {
        assert(x1.getIdx() == x2.getIdx());
        psubd(x1, op);
    }
    void uni_vpsubd(const Xbyak::Ymm &x1, const Xbyak::Ymm &x2,
            const Xbyak::Operand &op) {
        vpsubd(x1, x2, op);
    }

    void uni_vpsubb(const Xbyak::Xmm &x1, const Xbyak::Xmm &x2,
            const Xbyak::Operand &op) {
        assert(x1.getIdx() == x2.getIdx());
        psubb(x1, op);
    }
    void uni_vpsubb(const Xbyak::Ymm &x1, const Xbyak::Ymm &x2,
            const Xbyak::Operand &op) {
        vpsubb(x1, x2, op);
    }

    void uni_vsubss(const Xbyak::Xmm &x, const Xbyak::Operand &op1,
            const Xbyak::Operand &op2) {
        assert(x.isEqualIfNotInherited(op1));
        subps(x, op2);
    }
    void uni_vsubss(const Xbyak::Ymm &x, const Xbyak::Operand &op1,
            const Xbyak::Operand &op2) {
        vsubss(x, Xbyak::Xmm(op1.getIdx()), Xbyak::Xmm(op2.getIdx()));
    }

    void uni_vsubps(const Xbyak::Xmm &x, const Xbyak::Operand &op1,
            const Xbyak::Operand &op2) {
        assert(x.isEqualIfNotInherited(op1));
        subps(x, op2);
    }
    void uni_vsubps(const Xbyak::Ymm &x, const Xbyak::Operand &op1,
            const Xbyak::Operand &op2) {
        vsubps(x, op1, op2);
    }

    void uni_vsubps(const Xbyak::Xmm &x, const Xbyak::Operand &op1,
            const Xbyak::Operand &op2, const Xbyak::Xmm &buf) {
        movups(buf, op1);
        subps(buf, op2);
        if (x.getIdx() != buf.getIdx()) { movups(x, buf); }
    }

    void uni_vsubps(const Xbyak::Ymm &x, const Xbyak::Operand &op1,
            const Xbyak::Operand &op2, const Xbyak::Ymm &buf) {
        vsubps(x, op1, op2);
    }

    void uni_vpmulld(const Xbyak::Xmm &x1, const Xbyak::Xmm &x2,
            const Xbyak::Operand &op) {
        if (is_valid_isa(avx)) {
            vpmulld(x1, x2, op);
        } else {
            if (x1.getIdx() != x2.getIdx()) movdqa(x1, x2);
            pmulld(x1, op);
        }
    }
    void uni_vpmulld(const Xbyak::Ymm &x1, const Xbyak::Ymm &x2,
            const Xbyak::Operand &op) {
        vpmulld(x1, x2, op);
    }

    void uni_vmulps(const Xbyak::Xmm &x, const Xbyak::Operand &op1,
            const Xbyak::Operand &op2) {
        if (is_valid_isa(avx))
            vmulps(x, op1, op2);
        else {
            assert(x.isEqualIfNotInherited(op1));
            mulps(x, op2);
        }
    }
    void uni_vmulps(const Xbyak::Ymm &x, const Xbyak::Operand &op1,
            const Xbyak::Operand &op2) {
        vmulps(x, op1, op2);
    }

    void uni_vmulss(const Xbyak::Xmm &x, const Xbyak::Operand &op1,
            const Xbyak::Operand &op2) {
        assert(x.isEqualIfNotInherited(op1));
        mulss(x, op2);
    }
    void uni_vmulss(const Xbyak::Ymm &x, const Xbyak::Operand &op1,
            const Xbyak::Address &op2) {
        vmulss(x, Xbyak::Xmm(op1.getIdx()), op2);
    }
    void uni_vmulss(const Xbyak::Ymm &x, const Xbyak::Operand &op1,
            const Xbyak::Ymm &op2) {
        vmulss(x, Xbyak::Xmm(op1.getIdx()), Xbyak::Xmm(op2.getIdx()));
    }

    void uni_vfmadd132ps(const Xbyak::Xmm &x1, const Xbyak::Xmm &x2,
            const Xbyak::Operand &op) {
        // Note: x1 gets overriden by x1*op
        // This is incorrect if x1 == x2
        assert(x1.getIdx() != x2.getIdx());
        mulps(x1, op);
        addps(x1, x2);
    }
    void uni_vfmadd132ps(const Xbyak::Ymm &x1, const Xbyak::Ymm &x2,
            const Xbyak::Operand &op) {
        if (is_valid_isa(avx2))
            vfmadd132ps(x1, x2, op);
        else {
            // Note: x1 gets overriden by x1*op
            // This is incorrect if x1 == x2
            assert(x1.getIdx() != x2.getIdx());
            vmulps(x1, x1, op);
            vaddps(x1, x1, x2);
        }
    }

    void uni_vfmadd213ps(const Xbyak::Xmm &x1, const Xbyak::Xmm &x2,
            const Xbyak::Operand &op) {
        // Note: x1 gets overriden by x1*x2
        // This is incorrect if x1 == op
        assert(!x1.isEqualIfNotInherited(op));
        mulps(x1, x2);
        addps(x1, op);
    }
    void uni_vfmadd213ps(const Xbyak::Ymm &x1, const Xbyak::Ymm &x2,
            const Xbyak::Operand &op) {
        if (is_valid_isa(avx2))
            vfmadd213ps(x1, x2, op);
        else {
            // Note: x1 gets overriden by x1*x2
            // This is incorrect if x1 == op
            assert(!x1.isEqualIfNotInherited(op));
            vmulps(x1, x1, x2);
            vaddps(x1, x1, op);
        }
    }

    void uni_vfmadd213ss(const Xbyak::Xmm &x1, const Xbyak::Xmm &x2,
            const Xbyak::Operand &op) {
        // Note: x1 gets overriden by x1*x2
        // This is incorrect if x1 == op
        assert(!x1.isEqualIfNotInherited(op));
        mulss(x1, x2);
        addss(x1, op);
    }
    void uni_vfmadd213ss(const Xbyak::Ymm &x1, const Xbyak::Ymm &x2,
            const Xbyak::Operand &op) {
        if (is_valid_isa(avx2))
            vfmadd213ss(x1, x2, op);
        else {
            // Note: x1 gets overriden by x1*x2
            // This is incorrect if x1 == op
            assert(!x1.isEqualIfNotInherited(op));
            vmulss(x1, x1, x2);
            vaddss(x1, x1, op);
        }
    }

    void uni_vfmadd231ps(const Xbyak::Xmm &x1, const Xbyak::Xmm &x2,
            const Xbyak::Operand &op) {
        // Note: x2 gets overriden by x2*op
        // This is incorrect if x1 == x2
        assert(x1.getIdx() != x2.getIdx());
        mulps(x2, op);
        addps(x1, x2);
    }
    void uni_vfmadd231ps(const Xbyak::Ymm &x1, const Xbyak::Ymm &x2,
            const Xbyak::Operand &op) {
        if (is_valid_isa(avx2))
            vfmadd231ps(x1, x2, op);
        else {
            // Note: x2 gets overriden by x2*op
            // This is incorrect if x1 == x2
            assert(x1.getIdx() != x2.getIdx());
            vmulps(x2, x2, op);
            vaddps(x1, x1, x2);
        }
    }
    void uni_vfmadd231ss(const Xbyak::Xmm &x1, const Xbyak::Xmm &x2,
            const Xbyak::Operand &op) {
        // Note: x2 gets overriden by x2*op
        // This is incorrect if x1 == x2
        assert(x1.getIdx() != x2.getIdx());
        mulss(x2, op);
        addss(x1, x2);
    }
    void uni_vfmadd231ss(const Xbyak::Ymm &x1, const Xbyak::Ymm &x2,
            const Xbyak::Operand &op) {
        if (is_valid_isa(avx2))
            vfmadd231ss(Xbyak::Xmm(x1.getIdx()), Xbyak::Xmm(x2.getIdx()), op);
        else {
            // Note: x2 gets overriden by x2*op
            // This is incorrect if x1 == x2
            assert(x1.getIdx() != x2.getIdx());
            vmulss(x2, x2, op);
            vaddss(x1, x1, x2);
        }
    }

    void uni_vfnmadd231ps(const Xbyak::Xmm &x1, const Xbyak::Xmm &x2,
            const Xbyak::Operand &op) {
        // Note: x2 gets overriden by x2*op
        // This is incorrect if x1 == x2
        assert(x1.getIdx() != x2.getIdx());
        mulps(x2, op);
        subps(x1, x2);
    }

    void uni_vfnmadd231ps(const Xbyak::Ymm &x1, const Xbyak::Ymm &x2,
            const Xbyak::Operand &op) {
        if (is_valid_isa(avx2))
            vfnmadd231ps(x1, x2, op);
        else {
            // Note: x2 gets overriden by x2*op
            // This is incorrect if x1 == x2
            assert(x1.getIdx() != x2.getIdx());
            vmulps(x2, x2, op);
            vsubps(x1, x1, x2);
        }
    }

    void uni_vfmsub213ps(const Xbyak::Xmm &x1, const Xbyak::Xmm &x2,
            const Xbyak::Operand &op) {
        // Note: x1 gets overriden by x1*x2
        // This is incorrect if x1 == op
        assert(!x1.isEqualIfNotInherited(op));
        mulps(x1, x2);
        subps(x1, op);
    }
    void uni_vfmsub213ps(const Xbyak::Ymm &x1, const Xbyak::Ymm &x2,
            const Xbyak::Operand &op) {
        if (is_valid_isa(avx2))
            vfmsub213ps(x1, x2, op);
        else {
            // Note: x1 gets overriden by x1*x2
            // This is incorrect if x1 == op
            assert(!x1.isEqualIfNotInherited(op));
            vmulps(x1, x1, x2);
            vsubps(x1, x1, op);
        }
    }

    void uni_vsqrtps(const Xbyak::Xmm &x, const Xbyak::Operand &op) {
        sqrtps(x, op);
    }
    void uni_vsqrtps(const Xbyak::Ymm &x, const Xbyak::Operand &op) {
        vsqrtps(x, op);
    }

    void uni_vpaddd(const Xbyak::Xmm &x1, const Xbyak::Xmm &x2,
            const Xbyak::Operand &op) {
        if (is_valid_isa(avx))
            vpaddd(x1, x2, op);
        else {
            if (x1.getIdx() != x2.getIdx()) movdqa(x1, x2);
            paddd(x1, op);
        }
    }
    void uni_vpaddd(const Xbyak::Ymm &x1, const Xbyak::Ymm &x2,
            const Xbyak::Operand &op) {
        vpaddd(x1, x2, op);
    }

    void uni_vpaddb(const Xbyak::Xmm &x1, const Xbyak::Xmm &x2,
            const Xbyak::Operand &op) {
        if (is_valid_isa(avx))
            vpaddb(x1, x2, op);
        else {
            if (x1.getIdx() != x2.getIdx()) movdqa(x1, x2);
            paddb(x1, op);
        }
    }
    void uni_vpaddb(const Xbyak::Ymm &x1, const Xbyak::Ymm &x2,
            const Xbyak::Operand &op) {
        vpaddb(x1, x2, op);
    }

    void uni_vpmaddwd(const Xbyak::Xmm &x1, const Xbyak::Xmm &x2,
            const Xbyak::Operand &op) {
        if (is_valid_isa(avx))
            vpmaddwd(x1, x2, op);
        else {
            if (x1.getIdx() != x2.getIdx()) movdqa(x1, x2);
            pmaddwd(x1, op);
        }
    }
    void uni_vpmaddwd(const Xbyak::Ymm &x1, const Xbyak::Ymm &x2,
            const Xbyak::Operand &op) {
        vpmaddwd(x1, x2, op);
    }

    void uni_vpmaddubsw(const Xbyak::Xmm &x1, const Xbyak::Xmm &x2,
            const Xbyak::Operand &op) {
        if (is_valid_isa(avx))
            vpmaddubsw(x1, x2, op);
        else {
            if (x1.getIdx() != x2.getIdx()) movdqa(x1, x2);
            pmaddubsw(x1, op);
        }
    }
    void uni_vpmaddubsw(const Xbyak::Ymm &x1, const Xbyak::Ymm &x2,
            const Xbyak::Operand &op) {
        vpmaddubsw(x1, x2, op);
    }

    void uni_vandps(const Xbyak::Xmm &x1, const Xbyak::Xmm &x2,
            const Xbyak::Operand &op) {
        assert(x1.getIdx() == x2.getIdx());
        andps(x1, op);
    }
    void uni_vandps(const Xbyak::Ymm &x1, const Xbyak::Ymm &x2,
            const Xbyak::Operand &op) {
        if (!is_valid_isa(avx512_common) || x1.getBit() < 512)
            vandps(x1, x2, op);
        else
            vpandd(x1, x2, op);
    }

    void uni_vorps(const Xbyak::Xmm &x1, const Xbyak::Xmm &x2,
            const Xbyak::Operand &op) {
        assert(x1.getIdx() == x2.getIdx());
        orps(x1, op);
    }
    void uni_vorps(const Xbyak::Ymm &x1, const Xbyak::Ymm &x2,
            const Xbyak::Operand &op) {
        if (!is_valid_isa(avx512_common) || x1.getBit() < 512)
            vorps(x1, x2, op);
        else
            vpord(x1, x2, op);
    }

    void uni_vxorps(const Xbyak::Xmm &x1, const Xbyak::Xmm &x2,
            const Xbyak::Operand &op) {
        if (x1.getIdx() != x2.getIdx()) { uni_vmovups(x1, x2); }
        xorps(x1, op);
    }
    void uni_vxorps(const Xbyak::Ymm &x1, const Xbyak::Ymm &x2,
            const Xbyak::Operand &op) {
        if (!is_valid_isa(avx512_common) || x1.getBit() < 512)
            vxorps(x1, x2, op);
        else
            vpxord(x1, x2, op);
    }

    void uni_vpslld(
            const Xbyak::Xmm &x, const Xbyak::Operand &op, const int imm) {
        assert(x.isEqualIfNotInherited(op));
        pslld(x, imm);
    }
    void uni_vpslld(
            const Xbyak::Ymm &x, const Xbyak::Operand &op, const int imm) {
        vpslld(x, op, imm);
    }

    void uni_vpsrld(
            const Xbyak::Xmm &x, const Xbyak::Operand &op, const int imm) {
        if (!x.isEqualIfNotInherited(op)) uni_vmovups(x, op);
        psrld(x, imm);
    }
    void uni_vpsrld(
            const Xbyak::Ymm &x, const Xbyak::Operand &op, const int imm) {
        vpsrld(x, op, imm);
    }

    void uni_vmaxps(const Xbyak::Xmm &x, const Xbyak::Operand &op1,
            const Xbyak::Operand &op2) {
        assert(x.isEqualIfNotInherited(op1));
        maxps(x, op2);
    }
    void uni_vmaxps(const Xbyak::Ymm &x, const Xbyak::Operand &op1,
            const Xbyak::Operand &op2) {
        vmaxps(x, op1, op2);
    }

    void uni_vminps(const Xbyak::Xmm &x, const Xbyak::Operand &op1,
            const Xbyak::Operand &op2) {
        assert(x.isEqualIfNotInherited(op1));
        minps(x, op2);
    }
    void uni_vminps(const Xbyak::Ymm &x, const Xbyak::Operand &op1,
            const Xbyak::Operand &op2) {
        vminps(x, op1, op2);
    }

    void uni_vpmovsxbd(const Xbyak::Xmm &x, const Xbyak::Operand &op) {
        pmovsxbd(x, op);
    }

    void uni_vpmovsxbd(const Xbyak::Ymm &y, const Xbyak::Operand &op) {
        vpmovsxbd(y, op);
    }

    void uni_vpmovzxbd(const Xbyak::Xmm &x, const Xbyak::Operand &op) {
        pmovzxbd(x, op);
    }

    void uni_vpmovzxbd(const Xbyak::Ymm &y, const Xbyak::Operand &op) {
        vpmovzxbd(y, op);
    }

    void uni_vcmpps(const Xbyak::Xmm &x1, const Xbyak::Xmm &x2,
            const Xbyak::Operand &op, int cmp_predicate) {
        if (x1.getIdx() != x2.getIdx()) uni_vmovups(x1, x2);
        cmpps(x1, op, cmp_predicate);
    }
    void uni_vcmpps(const Xbyak::Ymm &x1, const Xbyak::Ymm &x2,
            const Xbyak::Operand &op, int cmp_predicate) {
        vcmpps(x1, x2, op, cmp_predicate);
    }

    void uni_vtestps(const Xbyak::Xmm &x1, const Xbyak::Operand &op) {
        ptest(x1, op);
    }

    void uni_vtestps(const Xbyak::Ymm &x1, const Xbyak::Operand &op) {
        assert(!(x1.isZMM() || op.isZMM()));
        vtestps(x1, op);
    }

    void uni_vblendvps(const Xbyak::Xmm &x1, const Xbyak::Xmm &x2,
            const Xbyak::Operand &op, const Xbyak::Xmm &msk) {
        assert(x1.getIdx() == x2.getIdx());
        assert(msk.getIdx() == 0);
        blendvps(x1, op);
    }
    void uni_vblendvps(const Xbyak::Ymm &x1, const Xbyak::Ymm &x2,
            const Xbyak::Operand &op, const Xbyak::Ymm &msk) {
        vblendvps(x1, x2, op, msk);
    }

    void uni_vroundps(
            const Xbyak::Xmm &x, const Xbyak::Operand &op, const int imm) {
        roundps(x, op, imm);
    }
    void uni_vroundps(
            const Xbyak::Ymm &x, const Xbyak::Operand &op, const int imm) {
        vroundps(x, op, imm);
    }
    void uni_vroundps(
            const Xbyak::Zmm &x, const Xbyak::Operand &op, const int imm) {
        vrndscaleps(x, op, imm & 0x3);
    }

    void uni_vcvtps2dq(const Xbyak::Xmm &x, const Xbyak::Operand &op) {
        cvtps2dq(x, op);
    }
    void uni_vcvtps2dq(const Xbyak::Ymm &x, const Xbyak::Operand &op) {
        vcvtps2dq(x, op);
    }

    void uni_vcvtdq2ps(const Xbyak::Xmm &x, const Xbyak::Operand &op) {
        cvtdq2ps(x, op);
    }
    void uni_vcvtdq2ps(const Xbyak::Ymm &x, const Xbyak::Operand &op) {
        vcvtdq2ps(x, op);
    }

    void uni_vmovmskps(const Xbyak::Reg &x1, const Xbyak::Xmm &x2) {
        movmskps(x1.cvt64(), x2);
    }
    void uni_vmovmskps(const Xbyak::Reg &x1, const Xbyak::Ymm &x2) {
        vmovmskps(x1, x2);
    }

    void uni_vmovq(const Xbyak::Xmm &x, const Xbyak::Reg64 &r) {
        if (is_valid_isa(avx))
            vmovq(x, r);
        else
            movq(x, r);
    }
    void uni_vmovq(const Xbyak::Address &addr, const Xbyak::Xmm &x) {
        if (is_valid_isa(avx))
            vmovq(addr, x);
        else
            movq(addr, x);
    }

    void uni_vpackssdw(const Xbyak::Xmm &x1, const Xbyak::Xmm &x2,
            const Xbyak::Operand &op) {
        assert(x1.getIdx() == x1.getIdx());
        packssdw(x1, op);
    }
    void uni_vpackssdw(const Xbyak::Ymm &x1, const Xbyak::Ymm &x2,
            const Xbyak::Operand &op) {
        vpackssdw(x1, x2, op);
    }

    void uni_vpackuswb(const Xbyak::Xmm &x1, const Xbyak::Xmm &x2,
            const Xbyak::Operand &op) {
        assert(x1.getIdx() == x1.getIdx());
        packuswb(x1, op);
    }
    void uni_vpackuswb(const Xbyak::Ymm &x1, const Xbyak::Ymm &x2,
            const Xbyak::Operand &op) {
        vpackuswb(x1, x2, op);
    }

    void uni_vpacksswb(const Xbyak::Xmm &x1, const Xbyak::Xmm &x2,
            const Xbyak::Operand &op) {
        assert(x1.getIdx() == x1.getIdx());
        packsswb(x1, op);
    }
    void uni_vpacksswb(const Xbyak::Ymm &x1, const Xbyak::Ymm &x2,
            const Xbyak::Operand &op) {
        vpacksswb(x1, x2, op);
    }

    void uni_vpinsrb(const Xbyak::Xmm &x1, const Xbyak::Xmm &x2,
            const Xbyak::Operand &op, const int imm) {
        assert(x1.getIdx() == x2.getIdx());
        if (is_valid_isa(avx))
            vpinsrb(x1, x2, op, imm);
        else
            pinsrb(x1, op, imm);
    }

    void uni_vpinsrb(const Xbyak::Ymm &x1, const Xbyak::Ymm &x2,
            const Xbyak::Operand &op, const int imm) {
        vpinsrb(x1, x2, op, imm);
    }

    void uni_vpinsrd(const Xbyak::Xmm &x1, const Xbyak::Xmm &x2,
            const Xbyak::Operand &op, const int imm) {
        assert(x1.getIdx() == x2.getIdx());
        if (is_valid_isa(avx))
            vpinsrd(x1, x2, op, imm);
        else
            pinsrd(x1, op, imm);
    }
    void uni_vpinsrd(const Xbyak::Ymm &x1, const Xbyak::Ymm &x2,
            const Xbyak::Operand &op, const int imm) {
        vpinsrd(x1, x2, op, imm);
    }

    void uni_vpinsrq(const Xbyak::Xmm &x1, const Xbyak::Xmm &x2,
            const Xbyak::Operand &op, const int imm) {
        assert(x1.getIdx() == x2.getIdx());
        if (is_valid_isa(avx))
            vpinsrq(x1, x2, op, imm);
        else
            pinsrq(x1, op, imm);
    }
    void uni_vpinsrq(const Xbyak::Ymm &x1, const Xbyak::Ymm &x2,
            const Xbyak::Operand &op, const int imm) {
        vpinsrq(x1, x2, op, imm);
    }

    void uni_vpinsrw(const Xbyak::Xmm &x1, const Xbyak::Xmm &x2,
            const Xbyak::Operand &op, const int imm) {
        assert(x1.getIdx() == x2.getIdx());
        if (is_valid_isa(avx))
            vpinsrw(x1, x2, op, imm);
        else
            pinsrw(x1, op, imm);
    }
    void uni_vpinsrw(const Xbyak::Ymm &x1, const Xbyak::Ymm &x2,
            const Xbyak::Operand &op, const int imm) {
        vpinsrw(x1, x2, op, imm);
    }

    void uni_vpextrb(
            const Xbyak::Operand &op, const Xbyak::Xmm &x, const int imm) {
        if (is_valid_isa(avx))
            vpextrb(op, x, imm);
        else
            pextrb(op, x, imm);
    }

    void uni_vpextrb(
            const Xbyak::Operand &op, const Xbyak::Ymm &x, const int imm) {
        vpextrb(op, x, imm);
    }

    void uni_vpextrw(
            const Xbyak::Operand &op, const Xbyak::Xmm &x, const int imm) {
        if (is_valid_isa(avx))
            vpextrw(op, x, imm);
        else
            pextrw(op, x, imm);
    }
    void uni_vpextrw(
            const Xbyak::Operand &op, const Xbyak::Ymm &x, const int imm) {
        vpextrw(op, x, imm);
    }

    void uni_vpextrd(
            const Xbyak::Operand &op, const Xbyak::Xmm &x, const int imm) {
        if (is_valid_isa(avx))
            vpextrd(op, x, imm);
        else
            pextrd(op, x, imm);
    }
    void uni_vpextrd(
            const Xbyak::Operand &op, const Xbyak::Ymm &x, const int imm) {
        vpextrd(op, x, imm);
    }

    void uni_vpextrq(
            const Xbyak::Operand &op, const Xbyak::Xmm &x, const int imm) {
        if (is_valid_isa(avx))
            vpextrq(op, x, imm);
        else
            pextrq(op, x, imm);
    }
    void uni_vpextrq(
            const Xbyak::Operand &op, const Xbyak::Ymm &x, const int imm) {
        vpextrq(op, x, imm);
    }

    void uni_vpmaxsd(const Xbyak::Xmm &x1, const Xbyak::Xmm &x2,
            const Xbyak::Operand &op) {
        if (is_valid_isa(avx))
            vpmaxsd(x1, x2, op);
        else {
            if (x1.getIdx() != x2.getIdx()) movdqa(x1, x2);
            pmaxsd(x1, op);
        }
    }

    void uni_vpmaxsd(const Xbyak::Ymm &x1, const Xbyak::Ymm &x2,
            const Xbyak::Operand &op) {
        vpmaxsd(x1, x2, op);
    }

    template <typename TReg>
    void uni_fdiv(const TReg &dst, const TReg &src, const TReg &src2) {
        fdiv(dst, src, src2);
    }

    void uni_fdiv(const Xbyak_aarch64::VReg4S &dst,
            const Xbyak_aarch64::VReg4S &src, const Xbyak_aarch64::VReg4S &src2,
            const Xbyak_aarch64::VReg4S &tmp, const Xbyak_aarch64::PReg &pred) {
        UNUSED(tmp);
        UNUSED(pred);
        xa_->fdiv(dst, src, src2);
    }

    template <typename TReg>
    void uni_fdiv(const TReg &dst, const TReg &src, const TReg &src2,
            const TReg &tmp, const Xbyak_aarch64::PReg &pred) {
        uint32_t dstIdx = dst.getIdx();
        uint32_t srcIdx = src.getIdx();
        uint32_t src2Idx = src2.getIdx();
        uint32_t tmpIdx = tmp.getIdx();

        if (dstIdx == src2Idx) {
            assert(tmpIdx != srcIdx && tmpIdx != src2Idx);

            xa_->mov(Xbyak_aarch64::ZRegD(tmpIdx),
                    Xbyak_aarch64::ZRegD(src2Idx));
            xa_->mov(dst, pred / Xbyak_aarch64::T_m, src);
            xa_->fdiv(dst, pred / Xbyak_aarch64::T_m, tmp);
        } else if (dstIdx == srcIdx) {
            xa_->fdiv(dst, pred / Xbyak_aarch64::T_m, src2);
        } else {
            xa_->mov(dst, P_ALL_ONE / Xbyak_aarch64::T_m, src);
            xa_->fdiv(dst, pred / Xbyak_aarch64::T_m, src2);
        }
    }

    void uni_fsub(const Xbyak_aarch64::VReg4S &v1,
            const Xbyak_aarch64::VReg4S &v2, const Xbyak_aarch64::VReg4S &v3) {
        xa_->fsub(v1, v2, v3);
    }

    void uni_fsub(const Xbyak_aarch64::ZRegS &z1,
            const Xbyak_aarch64::ZRegS &z2, const Xbyak_aarch64::ZRegS &z3) {
        xa_->fsub(z1, z2, z3);
    }

    void uni_eor(const Xbyak_aarch64::VReg &v1, const Xbyak_aarch64::VReg &v2,
            const Xbyak_aarch64::VReg &v3) {
        eor(Xbyak_aarch64::VReg16B(v1.getIdx()),
                Xbyak_aarch64::VReg16B(v2.getIdx()),
                Xbyak_aarch64::VReg16B(v3.getIdx()));
    }

    void uni_eor(const Xbyak_aarch64::ZReg &z1, const Xbyak_aarch64::ZReg &z2,
            const Xbyak_aarch64::ZReg &z3) {
        eor(Xbyak_aarch64::ZRegD(z1.getIdx()),
                Xbyak_aarch64::ZRegD(z2.getIdx()),
                Xbyak_aarch64::ZRegD(z3.getIdx()));
    }

    /*
      Saturation facility functions. enable to prepare the register
      holding the saturation upperbound and apply the saturation on
      the floating point register
     */
    template <typename Vmm>
    void init_saturate_f32(Vmm vmm_lbound, Vmm vmm_ubound,
            Xbyak_aarch64::XReg reg_tmp, data_type_t idt, data_type_t odt) {
        using namespace data_type;
        if (!((idt == f32) && utils::one_of(odt, u8, data_type::s8, s32)))
            return;

        assert(IMPLICATION(
                idt == u8, vmm_lbound.getIdx() != vmm_ubound.getIdx()));
        // No need to saturate on lower bound for signed integer types, as
        // the conversion to int would return INT_MIN, and then proper
        // saturation will happen in store_data
        if (odt == u8) {
            if (mayiuse(sve_512))
                dup(Xbyak_aarch64::ZRegS(vmm_lbound.getIdx()), 0);
            else if (mayiuse(asimd))
                movi(Xbyak_aarch64::VReg4S(vmm_lbound.getIdx()), 0);
            else
                assert(!"unreachable");
        }

        Xbyak_aarch64::ZRegS z_tmp(vmm_ubound.getIdx());
        Xbyak_aarch64::WReg w_tmp(reg_tmp.getIdx());
        float saturation_ubound = types::max_value<float>(odt);
        xa_->mov_imm(w_tmp, float2int(saturation_ubound));
        dup(z_tmp, w_tmp);
    }

    template <typename Vmm>
    void saturate_f32(const Vmm &vmm, const Vmm &vmm_lbound,
            const Vmm &vmm_ubound, data_type_t odt,
            const Xbyak_aarch64::PReg &p_true) {
        // This function is used to saturate to odt in f32 before converting
        // to s32 in order to avoid bad saturation due to cvtps2dq
        // behavior (it returns INT_MIN if the f32 is out of the
        // s32 range)
        using namespace data_type;
        if (!utils::one_of(odt, u8, data_type::s8, s32)) return;

        Xbyak_aarch64::VReg4S v_tmp(vmm.getIdx());
        Xbyak_aarch64::VReg4S v_lbound(vmm_lbound.getIdx());
        Xbyak_aarch64::VReg4S v_ubound(vmm_ubound.getIdx());
        Xbyak_aarch64::ZRegS z_tmp(vmm.getIdx());
        Xbyak_aarch64::ZRegS z_lbound(vmm_lbound.getIdx());
        Xbyak_aarch64::ZRegS z_ubound(vmm_ubound.getIdx());

        // no need to apply lower saturation bound when odt is
        // signed, as cvtps2dq will return MIN_INT if the value
        // does not fit
        if (odt == u8) {
            if (mayiuse(sve_512))
                fmax(z_tmp, p_true / Xbyak_aarch64::T_m, z_lbound);
            else if (mayiuse(asimd))
                fmax(v_tmp, v_tmp, v_lbound);
            else
                assert(!"unreachable");
        }
        if (mayiuse(sve_512))
            fmin(z_tmp, p_true / Xbyak_aarch64::T_m, z_ubound);
        else if (mayiuse(asimd))
            fmin(v_tmp, v_tmp, v_ubound);
        else
            assert(!"unreachable");
    }
    /**
    * load_bytes is the utility function to facilitate loading of
    * load_size (0 <= load_size <= 32) many contiguous bytes into the Xmm/Ymm
    * register from the memory referenced by ptr[reg + offset] address.
    *
    * Functionally, invocation of load_bytes is equivalent to
    * the following loop:
    *
    * for (int idx = 0; idx < load_size; ++idx)
    *     vpinsrb(xmm, xmm, ptr[reg + offset + idx], idx);
    *
    * TODO: Add an option to zero-out unloaded bytes in the Xmm register.
    * TODO: Add an option for unsafe_load wherein one could read outside the
    * provided memory buffer so as to minimize the total number of read
    * memory instructions.
    */
    template <typename Vmm>
    void load_bytes(const Vmm &vmm, const Xbyak::Reg64 &reg, int64_t offset,
            int load_size) {

        constexpr bool is_xmm = std::is_same<Vmm, Xbyak::Xmm>::value;
        constexpr bool is_ymm = std::is_same<Vmm, Xbyak::Ymm>::value;
        static_assert(
                is_xmm || is_ymm, "only Xmm or Ymm registers are allowed");

        MAYBE_UNUSED(is_xmm);
        MAYBE_UNUSED(is_ymm);

        // Ensure data fits completely inside the Xmm/Ymm register
        assert(load_size >= 0 && load_size <= 32);

        // Ensure offset is at most 4 bytes to be encoded in the instruction
        assert(offset >= INT_MIN && offset <= INT_MAX);

        // At most 16 bytes can fit inside the Xmm register
        assert(IMPLICATION(load_size > 16, is_ymm));

        // Ensure that vector register is compatible with the ISA in hand
        assert(IMPLICATION(is_ymm, is_valid_isa(avx)));

        assert(is_valid_isa(sse41)
                && "routine is not supported for the current isa");

        auto xmm = Xbyak::Xmm(vmm.getIdx());
        auto ymm = Xbyak::Ymm(vmm.getIdx());

        // addr(i) denotes the memory pointed by ptr[reg + offset + (i bytes)]
        const auto addr = [&](int bytes_offset) {
            return ptr[reg + offset + bytes_offset * sizeof(int8_t)];
        };

        if (load_size == 32) {
            vmovups(ymm, addr(0));
            return;
        }

        int start_bytes = 0;
        int bytes_to_load = load_size;

        if (load_size > 16) {
            // Prepare to insert to upper bits of ymm
            start_bytes = 16;
            bytes_to_load -= 16;
        }

        if (bytes_to_load >= 8 && bytes_to_load < 16)
            uni_vpinsrq(xmm, xmm, addr(start_bytes), 0);
        else if (bytes_to_load == 16)
            uni_vmovdqu(xmm, addr(start_bytes));

        switch (bytes_to_load) {
            case 0: break;
            case 1: uni_vpinsrb(xmm, xmm, addr(start_bytes), 0); break;
            case 2: uni_vpinsrw(xmm, xmm, addr(start_bytes), 0); break;
            case 3:
                uni_vpinsrw(xmm, xmm, addr(start_bytes), 0);
                uni_vpinsrb(xmm, xmm, addr(start_bytes + 2), 2);
                break;
            case 4: uni_vpinsrd(xmm, xmm, addr(start_bytes), 0); break;
            case 5:
                uni_vpinsrd(xmm, xmm, addr(start_bytes), 0);
                uni_vpinsrb(xmm, xmm, addr(start_bytes + 4), 4);
                break;
            case 6:
                uni_vpinsrd(xmm, xmm, addr(start_bytes), 0);
                uni_vpinsrw(xmm, xmm, addr(start_bytes + 4), 2);
                break;
            case 7:
                uni_vpinsrd(xmm, xmm, addr(start_bytes), 0);
                uni_vpinsrw(xmm, xmm, addr(start_bytes + 4), 2);
                uni_vpinsrb(xmm, xmm, addr(start_bytes + 6), 6);
                break;
            case 8: break;
            case 9: uni_vpinsrb(xmm, xmm, addr(start_bytes + 8), 8); break;
            case 10: uni_vpinsrw(xmm, xmm, addr(start_bytes + 8), 4); break;
            case 11:
                uni_vpinsrw(xmm, xmm, addr(start_bytes + 8), 4);
                uni_vpinsrb(xmm, xmm, addr(start_bytes + 10), 10);
                break;
            case 12: uni_vpinsrd(xmm, xmm, addr(start_bytes + 8), 2); break;
            case 13:
                uni_vpinsrd(xmm, xmm, addr(start_bytes + 8), 2);
                uni_vpinsrb(xmm, xmm, addr(start_bytes + 12), 12);
                break;
            case 14:
                uni_vpinsrd(xmm, xmm, addr(start_bytes + 8), 2);
                uni_vpinsrw(xmm, xmm, addr(start_bytes + 12), 6);
                break;
            case 15:
                uni_vpinsrd(xmm, xmm, addr(start_bytes + 8), 2);
                uni_vpinsrw(xmm, xmm, addr(start_bytes + 12), 6);
                uni_vpinsrb(xmm, xmm, addr(start_bytes + 14), 14);
                break;
            case 16: break;
            default: assert(!"improper load size");
        }

        if (load_size > 16) {
            vinsertf128(ymm, ymm, xmm, 1); // insert to upper bits of ymm
            vinsertf128(ymm, ymm, addr(0), 0); // insert to lower bits of ymm
        }
    }

    /**
    * store_bytes is the utility function to facilitate storing of
    * store_size (0 <= store_size <= 32) many contiguous bytes from the Xmm/Ymm
    * register into the memory referenced by ptr[reg + offset] address.
    *
    * Additionally, when store_size > 16, the input Ymm register will not be
    * preserved due to the usage of vextracti128 instruction.
    *
    * Functionally, invocation of store_bytes is equivalent
    * to the following loop:
    *
    * for (int idx = 0; idx < store_size; ++idx)
    *     vpextrb(ptr[reg + offset + idx], xmm, idx);
    *
    * TODO: Add an option for unsafe_store wherein one could store extra dwords
    * past the provided memory buffer so as to minimize the total number of
    * write memory instructions.
    */
    template <typename Vmm>
    void store_bytes(const Vmm &vmm, const Xbyak::Reg64 &reg, int64_t offset,
            int store_size) {

        constexpr bool is_xmm = std::is_same<Vmm, Xbyak::Xmm>::value;
        constexpr bool is_ymm = std::is_same<Vmm, Xbyak::Ymm>::value;
        static_assert(
                is_xmm || is_ymm, "only Xmm or Ymm registers are allowed");

        MAYBE_UNUSED(is_xmm);
        MAYBE_UNUSED(is_ymm);

        // Ensure data fits completely inside the Xmm/Ymm register
        assert(store_size >= 0 && store_size <= 32);

        // Ensure offset is at most 4 bytes to be encoded in the instruction
        assert(offset >= INT_MIN && offset <= INT_MAX);

        // At most 16 bytes can fit inside the Xmm register
        assert(IMPLICATION(store_size > 16, is_ymm));

        // Ensure that vector register is compatible with the ISA in hand
        assert(IMPLICATION(is_ymm, is_valid_isa(avx)));

        assert(is_valid_isa(sse41)
                && "routine is not supported for the current isa");

        auto xmm = Xbyak::Xmm(vmm.getIdx());
        auto ymm = Xbyak::Ymm(vmm.getIdx());

        const auto addr = [&](int bytes_offset) {
            return ptr[reg + offset + bytes_offset * sizeof(int8_t)];
        };

        if (store_size == 32) {
            vmovups(addr(0), ymm);
            return;
        }

        int start_bytes = 0;
        int bytes_to_store = store_size;

        if (store_size > 16) {
            vmovdqu(addr(0), xmm); // load lower bits from ymm
            start_bytes = 16;
            bytes_to_store -= 16;
            vextractf128(xmm, ymm, 1); // load upper bits from ymm into xmm
        }

        if (bytes_to_store >= 8 && bytes_to_store < 16)
            uni_vpextrq(addr(start_bytes), xmm, 0);
        else if (bytes_to_store == 16)
            uni_vmovdqu(addr(start_bytes), xmm);

        switch (bytes_to_store) {
            case 0: break;
            case 1: uni_vpextrb(addr(start_bytes), xmm, 0); break;
            case 2: uni_vpextrw(addr(start_bytes), xmm, 0); break;
            case 3:
                uni_vpextrw(addr(start_bytes), xmm, 0);
                uni_vpextrb(addr(start_bytes + 2), xmm, 2);
                break;
            case 4: uni_vpextrd(addr(start_bytes), xmm, 0); break;
            case 5:
                uni_vpextrd(addr(start_bytes), xmm, 0);
                uni_vpextrb(addr(start_bytes + 4), xmm, 4);
                break;
            case 6:
                uni_vpextrd(addr(start_bytes), xmm, 0);
                uni_vpextrw(addr(start_bytes + 4), xmm, 2);
                break;
            case 7:
                uni_vpextrd(addr(start_bytes), xmm, 0);
                uni_vpextrw(addr(start_bytes + 4), xmm, 2);
                uni_vpextrb(addr(start_bytes + 6), xmm, 6);
                break;
            case 8: break;
            case 9: uni_vpextrb(addr(start_bytes + 8), xmm, 8); break;
            case 10: uni_vpextrw(addr(start_bytes + 8), xmm, 4); break;
            case 11:
                uni_vpextrw(addr(start_bytes + 8), xmm, 4);
                uni_vpextrb(addr(start_bytes + 10), xmm, 10);
                break;
            case 12: uni_vpextrd(addr(start_bytes + 8), xmm, 2); break;
            case 13:
                uni_vpextrd(addr(start_bytes + 8), xmm, 2);
                uni_vpextrb(addr(start_bytes + 12), xmm, 12);
                break;
            case 14:
                uni_vpextrd(addr(start_bytes + 8), xmm, 2);
                uni_vpextrw(addr(start_bytes + 12), xmm, 6);
                break;
            case 15:
                uni_vpextrd(addr(start_bytes + 8), xmm, 2);
                uni_vpextrw(addr(start_bytes + 12), xmm, 6);
                uni_vpextrb(addr(start_bytes + 14), xmm, 14);
                break;
            case 16: break;
            default: assert(!"improper store size");
        }
    }

    DNNL_DISALLOW_COPY_AND_ASSIGN(jit_generator);

public:
    jit_generator(void *code_ptr = nullptr, size_t code_size = MAX_CODE_SIZE,
            bool use_autogrow = true, cpu_isa_t max_cpu_isa = isa_all)
        : Xbyak::CodeGenerator(code_size,
                (code_ptr == nullptr && use_autogrow) ? Xbyak::AutoGrow
                                                      : code_ptr)
        , max_cpu_isa_(max_cpu_isa) {}
    virtual ~jit_generator() {}

    virtual const char *name() const = 0;
    virtual const char *source_file() const = 0;

    void register_jit_code(const uint8_t *code, size_t code_size) const {
        jit_utils::register_jit_code(code, code_size, name(), source_file());
    }

    const uint8_t *jit_ker() const { return jit_ker_; }

    template <typename... kernel_args_t>
    void operator()(kernel_args_t... args) const {
        using jit_kernel_func_t = void (*)(const kernel_args_t... args);
        auto *fptr = (jit_kernel_func_t)jit_ker_;
        (*fptr)(std::forward<kernel_args_t>(args)...);
    }

    virtual status_t create_kernel() {
        generate();
        jit_ker_ = getCode();
        return (jit_ker_) ? status::success : status::runtime_error;
    }

private:
    const cpu_isa_t max_cpu_isa_;
    const uint8_t *getCode() {
        this->ready();
        if (!is_initialized()) return nullptr;
        const uint8_t *code
                = reinterpret_cast<const uint8_t *>(CodeGenerator::getCode());
        register_jit_code(code, getSize());
        return code;
    }

    inline bool is_valid_isa(cpu_isa_t isa) {
        return is_subset(isa, max_cpu_isa_) && mayiuse(isa);
    }

    static inline bool is_initialized() {
        /* At the moment, Xbyak_aarch64 does not have GetError()\
         so that return dummy result. */
        return true;
    }

protected:
    virtual void generate() = 0;
    const uint8_t *jit_ker_ = nullptr;
};

} // namespace aarch64
} // namespace cpu
} // namespace impl
} // namespace dnnl

#endif

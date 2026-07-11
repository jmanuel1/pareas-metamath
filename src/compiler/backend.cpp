#include "pareas/compiler/backend.hpp"
#include "pareas/compiler/futhark_interop.hpp"
#include "futhark_bridge.hpp"

#include <fmt/format.h>
#include <fmt/ostream.h>
#include <iostream>

namespace backend {
    DeviceModule compile(futhark_context* ctx, DeviceAst& ast, pareas::Profiler& p) {
        auto tree = futhark::UniqueTree(ctx);
        p.measure("translate ast", [&] {
            int err = futhark_entry_backend_convert_tree(
                ctx,
                &tree,
                ast.node_types,
                ast.parents,
                ast.node_data,
                ast.data_types,
                ast.node_depths,
                ast.child_indexes
            );
            if(err)
                throw futhark::Error(ctx);
        });

        // Stage 1, preprocessing
        p.measure("preprocessing", [&] {
            auto old_tree = std::move(tree);

            int err = futhark_entry_backend_preprocess(ctx, &tree, old_tree);
            if(err)
                throw futhark::Error(ctx);
        });

        // Stage 2, instruction count
        auto instr_counts = futhark::UniqueArray<uint32_t, 1>(ctx);
        auto functab = futhark::UniqueFuncInfoArray(ctx);
        p.measure("instruction count", [&] {
            auto sub_func_id = futhark::UniqueArray<uint32_t, 1>(ctx);
            auto sub_func_start = futhark::UniqueArray<uint32_t, 1>(ctx);
            auto sub_func_size = futhark::UniqueArray<uint32_t, 1>(ctx);
            futhark::UniqueTuple_tup3_arr1d_u32_arr1d_u32_arr1d_u32 function_table_ret(ctx);

            int err = futhark_entry_backend_instr_count(
                ctx,
                &instr_counts,
                tree
            );
            if(err)
                throw futhark::Error(ctx);

            err = futhark_entry_backend_instr_count_make_function_table(
                ctx,
                &function_table_ret,
                tree,
                instr_counts
            );
            if(err)
                throw futhark::Error(ctx);
            sub_func_id = function_table_ret.project_0();
            sub_func_start = function_table_ret.project_1();
            sub_func_size = function_table_ret.project_2();

            err = futhark_entry_backend_compact_functab(
                ctx,
                &functab,
                sub_func_id,
                sub_func_start,
                sub_func_size
            );

            if(err)
                throw futhark::Error(ctx);
        });

        // Stage 3, instruction gen
        auto instr = futhark::UniqueInstrArray(ctx);
        p.measure("instruction gen", [&] {
            int err = futhark_entry_backend_instr_gen(
                ctx,
                &instr,
                tree,
                instr_counts,
                functab
            );
            if(err)
                throw futhark::Error(ctx);
        });

        // Stage 4, optimizer
        auto optimize = futhark::UniqueArray<bool, 1>(ctx);
        p.measure("optimize", [&] {
            auto old_instr = std::move(instr);
            auto old_functab = std::move(functab);
            futhark::UniqueTuple_tup3_arr1d_Instr_arr1d_FuncInfo_arr1d_bool ret(ctx);

            int err = futhark_entry_backend_optimize(
                ctx,
                &ret,
                old_instr,
                old_functab
            );
            if(err)
                throw futhark::Error(ctx);
            instr = ret.project_0();
            functab = ret.project_1();
            optimize = ret.project_2();
        });

        // Stage 5-6, regalloc + instr remove
        p.measure("regalloc/instr remove", [&] {
            auto old_instr = std::move(instr);
            auto old_functab = std::move(functab);
            futhark::UniqueTuple_tup2_arr1d_Instr_arr1d_FuncInfo ret(ctx);

            int err = futhark_entry_backend_regalloc(
                ctx,
                &ret,
                old_instr,
                old_functab,
                ast.fn_tab,
                optimize
            );
            if(err)
                throw futhark::Error(ctx);
            instr = ret.project_0();
            functab = ret.project_1();
        });

        // Stage 7, jump fix
        auto mod = DeviceModule(ctx);
        p.measure("jump fix", [&] {
            auto old_instr = std::move(instr);
            futhark::UniqueTuple_tup4_arr1d_Instr_arr1d_u32_arr1d_u32_arr1d_u32 ret(ctx);

            int err = futhark_entry_backend_fix_jumps(
                ctx,
                &ret,
                old_instr,
                functab
            );
            if(err)
                throw futhark::Error(ctx);
            instr = ret.project_0();
            mod.func_id = ret.project_1();
            mod.func_start = ret.project_2();
            mod.func_size = ret.project_3();
        });

        // Stage 8, postprocess
        p.measure("postprocess", [&] {
            int err = futhark_entry_backend_postprocess(
                ctx,
                &mod.instructions,
                instr
            );
            if(err)
                throw futhark::Error(ctx);
        });

        return mod;
    }
}

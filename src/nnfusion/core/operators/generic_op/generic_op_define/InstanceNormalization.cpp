// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>
#include <nnfusion/core/graph/gnode.hpp>
#include "nnfusion/core/operators/generic_op/generic_op.hpp"

// InstanceNormalization: Scale * (Tensor - mean(Tensor, [2,3]) / sqrt( + epsilon) + Bias
// Input: 1. Tensor[N,C,H,W]; 2. Scale - 1 dim; 3. Bias - 1 dim;
REGISTER_OP(InstanceNormalization)
    .infershape([](std::shared_ptr<graph::GNode> curr) -> void {
        assert(curr->get_inputs().size() == 3);
        auto input_shape_0 = curr->get_input_shape(0);
        curr->set_output_type_and_shape(0, curr->get_input_element_type(0), input_shape_0);
        })
    .translate_v2([](std::shared_ptr<graph::GNode> curr) -> std::string {
        assert(curr->get_inputs().size() == 3);
        // T
        auto input_shape_0 = curr->get_input_shape(0);
        // Scale
        auto input_shape_1 = curr->get_input_shape(1);
        // Bias
        auto input_shape_2 = curr->get_input_shape(2);
        auto output_shape_0 = curr->get_output_shape(0);

        NNFUSION_CHECK(input_shape_0.size() == 3);
        NNFUSION_CHECK(input_shape_1.size() == 1);
        NNFUSION_CHECK(input_shape_2.size() == 1);
        NNFUSION_CHECK(output_shape_0.size() == 3);

        auto op = static_pointer_cast<nnfusion::op::GenericOp>(curr->get_op_ptr());
        auto& cfg = op->localOpConfig.getRoot();
        float epsilon = cfg["epsilon"].is_null()?1e-5:float(cfg["epsilon"]);

        auto expression = op::create_code_from_template(
            "avg[N,C] +=! @input0@[N,C,I] / @dims@;"
            "var[N,C] +=! (@input0@[N,C,I] - avg[N,C]).call(`pow`, 2) / @dims@;"
            "@output0@[N, C, I] = @input2@[C] + @input1@[C] * (@input0@[N, C, I] - "
            "avg[N,C]) / (@epsilon@ + var[N,C]).call(`sqrt`);",
            {{"dims", to_string(input_shape_0[2] * 1.0)},
            {"epsilon",to_string(epsilon)}});
        return expression;
    });

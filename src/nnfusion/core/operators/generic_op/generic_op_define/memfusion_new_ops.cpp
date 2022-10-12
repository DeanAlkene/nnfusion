#include "nnfusion/core/operators/generic_op/generic_op.hpp"

static string make_layout(const std::set<int>& axes) {
    std::string ret = "";
    for (auto ax : axes)
        ret += ", N" + std::to_string(ax);
    return "[" + (axes.empty() ? "N" : ret.substr(2)) + "]";
};

REGISTER_OP(SoftmaxBasic)
    .attr<vector<int>>("axes")
    .attr<int>("stage")
    .infershape([](std::shared_ptr<graph::GNode> gnode) -> void {
        auto& shape_0 = gnode->get_input_shape(0);
        auto generic_op = std::dynamic_pointer_cast<nnfusion::op::GenericOp>(gnode->get_op_ptr());
        vector<int> axes = generic_op->localOpConfig.getRoot()["axes"];
        int stage = generic_op->localOpConfig.getRoot()["stage"];
        nnfusion::Shape output_shape;
        if (stage == 1 || stage == 3) {
            output_shape = shape_0;
        } else {
            set<int> ax_set(axes.begin(), axes.end());
            for (int i = 0; i < shape_0.size(); i++) {
                if (ax_set.count(i)) continue;
                output_shape.push_back(shape_0[i]);
            }
        }
        gnode->set_output_type_and_shape(
            0, gnode->get_input_element_type(0), output_shape);
    })
    .translate_v2([](std::shared_ptr<graph::GNode> curr) -> std::string {
        std::set<int> input_ax, output_ax;
        auto input_shape = curr->get_input_shape(0);
        auto generic_op = std::dynamic_pointer_cast<nnfusion::op::GenericOp>(curr->get_op_ptr());
        vector<int> axes = generic_op->localOpConfig.getRoot()["axes"];
        int stage = generic_op->localOpConfig.getRoot()["stage"];
        set<int> ax_set(axes.begin(), axes.end());
        for (int i = 0; i < input_shape.size(); ++i)
        {
             if (!ax_set.count(i))
                output_ax.insert(i);
            input_ax.insert(i);
        }
        string expression_template;
        if (stage == 0) {
            expression_template =
                R"( @output0@@temp_layout@ >=! @input0@@input0_layout@; )";
        } else if (stage == 1) {
            expression_template =
                R"( @output0@@input0_layout@ = (@input0@@input0_layout@ - @input1@@temp_layout@).call(`exp`); )";
        } else if (stage == 2) {
            expression_template =
                R"( @output0@@temp_layout@ +=! @input0@@input0_layout@; )";
        } else if (stage == 3) {
            expression_template =
                R"( @output0@@input0_layout@ = @input0@@input0_layout@ / @input1@@temp_layout@; )";
        } else {
            NNFUSION_CHECK_FAIL() << "Incorrect Stage ID.";
        }
        std::string expression_code = op::create_code_from_template(
            expression_template,
            {{"temp_layout", make_layout(output_ax)}, {"input0_layout", make_layout(input_ax)}});
        return expression_code;
    });

REGISTER_OP(CNHW2NCHW)
    .attr<size_t>("N")
    .attr<size_t>("C")
    .attr<size_t>("H")
    .attr<size_t>("W")
    .infershape([](std::shared_ptr<graph::GNode> gnode) -> void {
        auto generic_op = std::dynamic_pointer_cast<nnfusion::op::GenericOp>(gnode->get_op_ptr());
        size_t N = generic_op->localOpConfig.getRoot()["N"];
        size_t C = generic_op->localOpConfig.getRoot()["C"];
        size_t H = generic_op->localOpConfig.getRoot()["H"];
        size_t W = generic_op->localOpConfig.getRoot()["W"];
        nnfusion::Shape output_shape{N, C, H, W};
        gnode->set_output_type_and_shape(
            0, gnode->get_input_element_type(0), output_shape);
    })
    .translate_v2([](std::shared_ptr<graph::GNode> curr) -> std::string {
        string expression_template =
            R"( @output0@[N, C, H, W] = @input0@[C, W+H*@W@+N*@H*W@] where N in @N@, H in @H@, W in @W@; )";
        auto generic_op = std::dynamic_pointer_cast<nnfusion::op::GenericOp>(curr->get_op_ptr());
        size_t H = generic_op->localOpConfig.getRoot()["H"];
        size_t W = generic_op->localOpConfig.getRoot()["W"];
        size_t N = generic_op->localOpConfig.getRoot()["N"];
        nnfusion::json config;
        config["W"] = W;
        config["H"] = H;
        config["N"] = N;
        config["H*W"] = H*W;
        string expression_code = op::create_code_from_template(
            expression_template, config);
        return expression_code;
    });

REGISTER_OP(ImplicitGemm)
    .attr<size_t>("N")
    .attr<size_t>("C")
    .attr<size_t>("H")
    .attr<size_t>("W")
    .attr<size_t>("P")
    .attr<size_t>("S")
    .attr<size_t>("D")
    .infershape([](std::shared_ptr<graph::GNode> gnode) -> void {
        auto generic_op = std::dynamic_pointer_cast<nnfusion::op::GenericOp>(gnode->get_op_ptr());
        size_t c = generic_op->localOpConfig.getRoot()["C"];
        size_t n = generic_op->localOpConfig.getRoot()["N"];
        size_t h = generic_op->localOpConfig.getRoot()["H"];
        size_t w = generic_op->localOpConfig.getRoot()["W"];
        nnfusion::Shape output_shape{c, n * h * w};
        gnode->set_output_type_and_shape(
            0, gnode->get_input_element_type(0), output_shape);
    })
    .translate_v2([](std::shared_ptr<graph::GNode> curr) -> std::string {
        auto generic_op = std::dynamic_pointer_cast<nnfusion::op::GenericOp>(curr->get_op_ptr());
        size_t kh = curr->get_input_shape(1)[2];
        size_t kw = curr->get_input_shape(1)[3];
        size_t n = curr->get_input_shape(0)[0];
        size_t c = curr->get_input_shape(0)[1];
        size_t inh = curr->get_input_shape(0)[2];
        size_t inw = curr->get_input_shape(0)[3];
        size_t f = generic_op->localOpConfig.getRoot()["C"];
        size_t h = generic_op->localOpConfig.getRoot()["H"];
        size_t w = generic_op->localOpConfig.getRoot()["W"];
        size_t p = generic_op->localOpConfig.getRoot()["P"];
        size_t s = generic_op->localOpConfig.getRoot()["S"];
        size_t d = generic_op->localOpConfig.getRoot()["D"];
        NNFUSION_CHECK(inh = (h - 1) * s + (kh - 1) * d + 1 - 2 * p);
        NNFUSION_CHECK(inw = (w - 1) * s + (kw - 1) * d + 1 - 2 * p);
        size_t padh = inh + 2 * p, padw = inw + 2 * p;
        string pad_template = "";
        string data_template = R"( data[K, N] = @input0@[N//@h*w@, K//@kh*kw@, N%@h*w@//@w@*@s@+K%@kh*kw@//@kw@*@d@, N%@w@*@s@+K%@kw@*@d@] where K in @kh*kw*c@, N in @n*h*w@; )";
        string kernel_template = R"( kernel[M, K] = @input1@[M, K//@kh*kw@, K%@kh*kw@//@kw@, K%@kw@] where K in @kh*kw*c@, M in @f@; )";
        string compute_template = R"( @output0@[M, N] +=! kernel[M, K] * data[K, N]; )";
        if (p != 0) {
            pad_template = R"( pad[N, C, H0, W0] = @input0@[N, C, H0-@p@, W0-@p@].when([H0>=@p@, H0<@inh+p@, W0>=@p@, W0<@inw+p@], const(0.0).cast(input0[N, C, H0-@p@, W0-@p@].dtype())) where H0 in @padh@, W0 in @padw@; )";
            string input_str = "@input0@";
            data_template.replace(data_template.find(input_str), input_str.size(), "pad");
        }
        string expression_template = pad_template + data_template + kernel_template + compute_template;
        nnfusion::json config;
        config["p"] = p;
        config["s"] = s;
        config["d"] = d;
        config["padw"] = inh + 2 * p;
        config["padh"] = inw + 2 * p;
        config["inh+p"] = inh+p;
        config["inw+p"] = inw+p;
        config["w"] = w;
        config["h*w"] = h*w;
        config["kh*kw"] = kh *kw;
        config["kw"] = kw;
        config["kh*kw*c"] = kh*kw*c;
        config["n*h*w"] = n*h*w;
        config["f"] = f;
        string ir = op::create_code_from_template(
            expression_template, config);
        if (curr->get_output_element_type(0) == nnfusion::element::f16) {
            ir += "## @: tensorCoreConfig=(0, 1)";
        }
        return ir;
    });

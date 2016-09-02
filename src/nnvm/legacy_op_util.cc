/*!
 *  Copyright (c) 2015 by Contributors
 * \file legacy_op_util.cc
 * \brief Utility to adapt OpProperty to the new NNVM registery
 */
#include <mxnet/base.h>
#include <mxnet/operator.h>
#include <mxnet/op_attr_types.h>
#include <nnvm/node.h>
#include <memory>

namespace mxnet {
namespace op {

using nnvm::Op;
using nnvm::Node;
using nnvm::NodePtr;
using nnvm::NodeAttrs;
using nnvm::NodeEntry;

class ParsedOpProp {
 public:
  std::shared_ptr<OperatorProperty> ptr;
  std::vector<std::string> arguments;
  std::vector<std::string> aux_states;
  std::vector<std::string> inputs;
  std::vector<std::string> outputs;
  // initializer
  void Init(const NodeAttrs& attrs) {
    std::vector<std::pair<std::string, std::string> > kwargs(
        attrs.dict.begin(), attrs.dict.end());
    ptr->Init(kwargs);
    arguments = ptr->ListArguments();
    aux_states = ptr->ListAuxiliaryStates();
    outputs = ptr->ListOutputs();
    inputs = arguments;
    inputs.insert(
        inputs.end(), aux_states.begin(), aux_states.end());
  }
};


// function to use operator property to infer attr
// get op property from the attribute
const OperatorProperty* OpPropGetOpProperty(const NodeAttrs& attrs) {
  return nnvm::get<ParsedOpProp>(attrs.parsed).ptr.get();
}

template<typename AttrType, typename FInfer>
bool OpPropInferAttr(const NodeAttrs& attrs,
                     std::vector<AttrType> *iattr,
                     std::vector<AttrType> *oattr,
                     FInfer finfer) {
  auto& prop = nnvm::get<ParsedOpProp>(attrs.parsed);
  CHECK_EQ(prop.inputs.size(), iattr->size());
  std::vector<AttrType> in_attr(prop.arguments.size());
  std::vector<AttrType> aux_attr(prop.aux_states.size());

  for (size_t i = 0; i < prop.arguments.size(); ++i) {
    in_attr[i] = (*iattr)[i];
  }
  for (size_t i = 0; i < prop.aux_states.size(); ++i) {
    aux_attr[i] = (*iattr)[i + prop.arguments.size()];
  }
  if (!finfer(prop.ptr.get(), &in_attr, oattr, &aux_attr)) return false;

  for (size_t i = 0; i < prop.arguments.size(); ++i) {
    (*iattr)[i] = in_attr[i];
  }
  for (size_t i = 0; i < prop.aux_states.size(); ++i) {
    (*iattr)[i + prop.arguments.size()] = aux_attr[i];
  }
  return true;
}

bool OpPropInferShape(const NodeAttrs& attrs,
                      std::vector<TShape> *iattr,
                      std::vector<TShape>* oattr) {
  auto finfer = [](const OperatorProperty* op,
                   std::vector<TShape> *in,
                   std::vector<TShape> *out,
                   std::vector<TShape> *aux) {
    return op->InferShape(in, out, aux);
  };
  return OpPropInferAttr(attrs, iattr, oattr, finfer);
}

bool OpPropInferType(const NodeAttrs& attrs,
                     std::vector<int> *iattr,
                     std::vector<int>* oattr) {
  auto finfer = [](const OperatorProperty* op,
                   std::vector<int> *in,
                   std::vector<int> *out,
                   std::vector<int> *aux) {
    return op->InferType(in, out, aux);
  };
  return OpPropInferAttr(attrs, iattr, oattr, finfer);
}

inline uint32_t OpPropNumInputs(const NodeAttrs& attrs) {
  auto& prop = nnvm::get<ParsedOpProp>(attrs.parsed);
  return static_cast<uint32_t>(prop.inputs.size());
}

inline uint32_t OpPropNumOutputs(const NodeAttrs& attrs) {
  auto& prop = nnvm::get<ParsedOpProp>(attrs.parsed);
  return static_cast<uint32_t>(prop.outputs.size());
}

inline uint32_t OpPropNumVisibleOutputs(const NodeAttrs& attrs) {
  auto& prop = nnvm::get<ParsedOpProp>(attrs.parsed);
  return static_cast<uint32_t>(prop.ptr->NumVisibleOutputs());
}

std::vector<std::string> OpPropListInputNames(const NodeAttrs& attrs) {
  auto& prop = nnvm::get<ParsedOpProp>(attrs.parsed);
  return prop.inputs;
}

std::vector<std::string> OpPropListOutputNames(const NodeAttrs& attrs) {
  auto& prop = nnvm::get<ParsedOpProp>(attrs.parsed);
  return prop.outputs;
}

std::vector<uint32_t> OpPropMutateInputs(const NodeAttrs& attrs) {
  auto& prop = nnvm::get<ParsedOpProp>(attrs.parsed);
  std::vector<uint32_t> ret;
  for (uint32_t i = 0; i < prop.aux_states.size(); ++i) {
    ret.push_back(static_cast<uint32_t>(i + prop.arguments.size()));
  }
  return ret;
}

std::vector<std::pair<int, int> > OpPropInplaceOption(const NodeAttrs& attrs) {
  auto& prop = nnvm::get<ParsedOpProp>(attrs.parsed);
  std::vector<int> in_data(prop.arguments.size());
  std::vector<int> out_data(prop.outputs.size());
  std::vector<void*> out_addr(prop.outputs.size());
  for (size_t i = 0; i < in_data.size(); ++i) {
    in_data[i] = static_cast<int>(i);
  }
  for (size_t i = 0; i < out_data.size(); ++i) {
    out_data[i] = static_cast<int>(i);
    out_addr[i] = &out_data[i];
  }
  std::vector<std::pair<int, int> > forward_inplace;
  for (auto& kv : prop.ptr->ForwardInplaceOption(in_data, out_addr)) {
    forward_inplace.push_back(
        std::make_pair(kv.first, *static_cast<int*>(kv.second)));
  }
  return forward_inplace;
}

std::vector<ResourceRequest> OpPropResourceRequest(const NodeAttrs& attrs) {
  std::vector<TShape> ishape;
  auto& prop = nnvm::get<ParsedOpProp>(attrs.parsed);
  return prop.ptr->ForwardResource(ishape);
}

std::vector<ResourceRequest> OpBackResourceRequest(const NodeAttrs& attrs) {
  std::vector<TShape> ishape;
  auto& prop = nnvm::get<ParsedOpProp>(attrs.parsed);
  return prop.ptr->BackwardResource(ishape);
}

Operator* OpPropCreateLayerOp(const NodeAttrs& attrs,
                              Context ctx,
                              const std::vector<TShape>& ishape,
                              const std::vector<int>& itype) {
  auto& prop = nnvm::get<ParsedOpProp>(attrs.parsed);
  std::vector<TShape> is(ishape.begin(), ishape.begin() + prop.arguments.size());
  std::vector<int> it(itype.begin(), itype.begin() + prop.arguments.size());
  return prop.ptr->CreateOperatorEx(ctx, &is, &it);
}

inline std::vector<NodeEntry> OpPropGradient(
    const Op* back_op,
    const NodePtr& ptr,
    const std::vector<NodeEntry>& out_grads) {
  auto& prop = nnvm::get<ParsedOpProp>(ptr->attrs.parsed);
  std::vector<NodeEntry> out_data(prop.outputs.size());
  for (uint32_t i = 0; i < out_data.size(); ++i) {
    out_data[i] = NodeEntry{ptr, i, 0};
  }
  std::vector<NodeEntry> in_data(
      ptr->inputs.begin(), ptr->inputs.begin() + prop.arguments.size());
  std::vector<NodeEntry> ograd(
      out_grads.begin(), out_grads.begin() + prop.ptr->NumVisibleOutputs());
  auto inputs = prop.ptr->BackwardInputs(ograd, in_data, out_data);
  // add all the auxiliary data
  for (uint32_t i = 0; i < prop.aux_states.size(); ++i) {
    inputs.emplace_back(ptr->inputs[i + prop.arguments.size()]);
  }
  NodePtr gnode = Node::Create();
  gnode->inputs = std::move(inputs);
  gnode->control_deps.emplace_back(ptr);
  gnode->attrs = ptr->attrs;
  gnode->attrs.op = back_op;
  gnode->attrs.name = ptr->attrs.name + "_backward";
  std::vector<NodeEntry> in_grad(prop.inputs.size());
  for (uint32_t i = 0; i < prop.arguments.size(); ++i) {
    in_grad[i] = NodeEntry{gnode, i, 0};
  }
  // attach no gradient node to forbid gradient on aux_state
  if (prop.aux_states.size() != 0) {
    NodePtr ng = Node::Create();
    ng->attrs.op = Op::Get("_NoGradient");
    ng->attrs.name = "NoGradient";
    for (uint32_t i = 0; i < prop.aux_states.size(); ++i) {
      in_grad.emplace_back(NodeEntry{ng, 0, 0});
    }
  }
  return in_grad;
}

std::vector<uint32_t> OpBackOutToInIndex(const NodeAttrs& attrs) {
  auto& prop = nnvm::get<ParsedOpProp>(attrs.parsed);
  std::vector<uint32_t> idx(prop.arguments.size());
  for (uint32_t i = 0; i < idx.size(); ++i) {
    idx[i] = i;
  }
  return idx;
}

inline uint32_t OpBackNumOutputs(const NodeAttrs& attrs) {
  auto& prop = nnvm::get<ParsedOpProp>(attrs.parsed);
  return static_cast<uint32_t>(prop.arguments.size());
}

std::vector<std::string> OpBackListOutputNames(const NodeAttrs& attrs) {
  auto& prop = nnvm::get<ParsedOpProp>(attrs.parsed);
  return prop.arguments;
}

std::vector<uint32_t> OpBackMutateInputs(const NodeAttrs& attrs) {
  auto& prop = nnvm::get<ParsedOpProp>(attrs.parsed);
  if (prop.aux_states.size() == 0) return std::vector<uint32_t>{};
  std::vector<int> out_grad_index(prop.ptr->NumVisibleOutputs());
  std::vector<int> in_data_index(prop.arguments.size());
  std::vector<int> out_data_index(prop.outputs.size());
  size_t arg_size = prop.ptr->DeclareBackwardDependency(
      out_grad_index, in_data_index, out_data_index).size();
  std::vector<uint32_t> ret;
  for (uint32_t i = 0; i < prop.aux_states.size(); ++i) {
    ret.push_back(static_cast<uint32_t>(i + arg_size));
  }
  return ret;
}

std::vector<std::pair<int, int> > OpBackInplaceOption(const NodeAttrs& attrs) {
  auto& prop = nnvm::get<ParsedOpProp>(attrs.parsed);
  std::vector<int> out_grad_index(prop.ptr->NumVisibleOutputs());
  std::vector<int> in_data_index(prop.arguments.size());
  std::vector<int> out_data_index(prop.outputs.size());

  int counter = 0;
  for (size_t i = 0; i < in_data_index.size(); ++i) {
    in_data_index[i] = counter++;
  }
  for (size_t i = 0; i < out_grad_index.size(); ++i) {
    out_grad_index[i] = counter++;
  }
  for (size_t i = 0; i < out_data_index.size(); ++i) {
    out_data_index[i] = counter++;
  }

  auto args_index = prop.ptr->DeclareBackwardDependency(
      out_grad_index, in_data_index, out_data_index);
  std::vector<int> args_array(counter, -1);
  for (size_t i = 0; i < args_index.size(); ++i) {
    args_array[args_index[i]] = static_cast<int>(i);
  }

  std::vector<void*> in_grad_ptr(in_data_index.size());
  for (size_t i = 0; i < in_grad_ptr.size(); ++i) {
    // in data index starts from 0 to num_inputs
    in_grad_ptr[i] = (void*)&in_data_index[i];  // NOLINT(*)
  }

  auto remap_index = prop.ptr->BackwardInplaceOption(
      out_grad_index, in_data_index, out_data_index, in_grad_ptr);
  std::vector<std::pair<int, int> > remap(remap_index.size());
  for (size_t i = 0; i < remap_index.size(); ++i) {
    if (args_array[remap_index[i].first] == -1) {
      LOG(FATAL) << "BackwardInplaceOption not consistent with DeclareBackwardDependency";
    }
    remap[i].first = args_array[remap_index[i].first];
    remap[i].second = *static_cast<int*>(remap_index[i].second);
  }
  return remap;
}

// register the legacy operator properties under NNVM registry.
void RegisterLegacyOpProp() {
  for (auto reg : dmlc::Registry<OperatorPropertyReg>::List()) {
    Op& op = ::dmlc::Registry<::nnvm::Op>::Get()->__REGISTER_OR_GET__(reg->name);
    if (op.attr_parser != nullptr) continue;
    auto creator = reg->body;
    auto attr_parser = [creator](NodeAttrs* attrs) {
      if (attrs->parsed.empty()) {
        ParsedOpProp op;
        op.ptr.reset(creator());
        op.Init(*attrs);
        attrs->parsed = std::move(op);
      }
    };
    op.add_arguments(reg->arguments);
    op.describe(reg->description);
    // attribute parser
    op.set_attr_parser(attr_parser);
    op.set_num_inputs(OpPropNumInputs);
    op.set_num_outputs(OpPropNumOutputs);
    op.attr<nnvm::FListInputNames>("FListInputNames", OpPropListInputNames);
    op.attr<nnvm::FListOutputNames>("FListOutputNames", OpPropListOutputNames);
    op.attr<nnvm::FNumVisibleOutputs>("FNumVisibleOutputs", OpPropNumVisibleOutputs);
    op.attr<nnvm::FInferShape>("FInferShape", OpPropInferShape);
    op.attr<nnvm::FInferType>("FInferType", OpPropInferType);
    op.attr<nnvm::FMutateInputs>("FMutateInputs", OpPropMutateInputs);
    op.attr<nnvm::FInplaceOption>("FInplaceOption", OpPropInplaceOption);
    op.attr<FResourceRequest>("FResourceRequest", OpPropResourceRequest);
    op.attr<FCreateLayerOp>("FCreateLayerOp", OpPropCreateLayerOp);
    if (reg->key_var_num_args.length() != 0) {
      op.attr<std::string>("key_var_num_args", reg->key_var_num_args);
    }
    // register BackwardOps
    std::string back_op_name = "_backward_" + reg->name;
    Op& back_op = ::dmlc::Registry<::nnvm::Op>::Get()->__REGISTER__(back_op_name);
    op.attr<nnvm::FGradient>("FGradient", std::bind(
        OpPropGradient, &back_op,
        std::placeholders::_1, std::placeholders::_2));
    back_op.set_attr_parser(attr_parser);
    back_op.set_num_inputs(nnvm::kVarg);
    back_op.set_num_outputs(OpBackNumOutputs);
    back_op.attr<nnvm::FBackwardOutToInIndex>(
        "FBackwardOutToInIndex", OpBackOutToInIndex);
    back_op.attr<nnvm::FListOutputNames>("FListOutputNames", OpBackListOutputNames);
    back_op.attr<nnvm::FMutateInputs>("FMutateInputs", OpBackMutateInputs);
    back_op.attr<nnvm::FInplaceOption>("FInplaceOption", OpBackInplaceOption);
    back_op.attr<FResourceRequest>(
        "FResourceRequest", OpBackResourceRequest);
    back_op.attr<bool>("TIsLayerOpBackward", true);
  }
}

// no gradient operator
NNVM_REGISTER_OP(_NoGradient)
.set_num_inputs(0)
.set_num_outputs(1)
.describe("Place holder for variable who cannot perform gradient");

}  // namespace op
}  // namespace mxnet
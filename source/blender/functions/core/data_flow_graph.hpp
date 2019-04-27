#pragma once

#include "function.hpp"
#include "data_flow_graph_builder.hpp"
#include "BLI_range.hpp"

namespace FN {

struct DFGraphSocket {
 private:
  bool m_is_output;
  uint m_id;

 public:
  DFGraphSocket(bool is_output, uint id) : m_is_output(is_output), m_id(id)
  {
  }

  static DFGraphSocket FromInput(uint id)
  {
    return DFGraphSocket(false, id);
  }

  static DFGraphSocket FromOutput(uint id)
  {
    return DFGraphSocket(true, id);
  }

  bool is_input() const
  {
    return !m_is_output;
  }

  bool is_output() const
  {
    return m_is_output;
  }

  uint id() const
  {
    return m_id;
  }

  friend inline bool operator==(const DFGraphSocket &a, const DFGraphSocket &b)
  {
    return a.m_id == b.m_id && a.m_is_output == b.m_is_output;
  }
};

template<typename IdIteratorT> class DFGraphSocketIterator {
 private:
  bool m_is_output;
  IdIteratorT m_it;

 public:
  DFGraphSocketIterator(bool is_output, IdIteratorT it) : m_is_output(is_output), m_it(it)
  {
  }

  DFGraphSocketIterator &operator++()
  {
    ++m_it;
    return *this;
  }

  bool operator!=(const DFGraphSocketIterator &other)
  {
    return m_it != other.m_it;
  }

  DFGraphSocket operator*() const
  {
    return DFGraphSocket(m_is_output, *m_it);
  }
};

template<typename SequenceT> class DFGraphSocketSequence {
 private:
  bool m_is_output;
  SequenceT m_sequence;
  using IdIteratorT = decltype(m_sequence.begin());

 public:
  DFGraphSocketSequence(bool is_output, SequenceT sequence)
      : m_is_output(is_output), m_sequence(sequence)
  {
  }

  DFGraphSocketIterator<IdIteratorT> begin()
  {
    return DFGraphSocketIterator<IdIteratorT>(m_is_output, m_sequence.begin());
  }

  DFGraphSocketIterator<IdIteratorT> end()
  {
    return DFGraphSocketIterator<IdIteratorT>(m_is_output, m_sequence.end());
  }

  uint size() const
  {
    return m_sequence.size();
  }
};

class DataFlowGraph;
using SharedDataFlowGraph = AutoRefCount<DataFlowGraph>;

class DataFlowGraph : public RefCountedBase {
 private:
  struct MyNode {
    SharedFunction function;
    SourceInfo *source_info;
    /* Index into m_origins. */
    uint inputs_start;
    /* Index into m_targets_info. */
    uint outputs_start;

    MyNode(SharedFunction fn, SourceInfo *source_info, uint inputs_start, uint outputs_start)
        : function(std::move(fn)),
          source_info(source_info),
          inputs_start(inputs_start),
          outputs_start(outputs_start)
    {
    }
  };

  struct InputSocket {
    uint node;
    uint origin;

    InputSocket(uint node, uint origin) : node(node), origin(origin)
    {
    }
  };

  struct OutputSocket {
    uint node;
    uint targets_start;
    uint targets_amount;

    OutputSocket(uint node, uint targets_start, uint targets_amount)
        : node(node), targets_start(targets_start), targets_amount(targets_amount)
    {
    }
  };

  SmallVector<MyNode> m_nodes;
  SmallVector<InputSocket> m_inputs;
  SmallVector<OutputSocket> m_outputs;
  SmallVector<uint> m_targets;
  std::unique_ptr<MemMultiPool> m_source_info_pool;

 public:
  DataFlowGraph() = default;
  DataFlowGraph(DataFlowGraph &other) = delete;
  ~DataFlowGraph();

  struct ToBuilderMapping {
    SmallMap<DFGB_Node *, uint> node_indices;
    SmallMap<DFGB_Socket, uint> input_socket_indices;
    SmallMap<DFGB_Socket, uint> output_socket_indices;

    DFGraphSocket map_socket(DFGB_Socket dfgb_socket)
    {
      if (dfgb_socket.is_input()) {
        return DFGraphSocket(false, input_socket_indices.lookup(dfgb_socket));
      }
      else {
        return DFGraphSocket(true, output_socket_indices.lookup(dfgb_socket));
      }
    }
  };

  static SharedDataFlowGraph FromBuilder(DataFlowGraphBuilder &builder,
                                         ToBuilderMapping &r_mapping);

  Range<uint> node_ids() const
  {
    return Range<uint>(0, m_nodes.size());
  }

  SharedFunction &function_of_node(uint node_id)
  {
    return m_nodes[node_id].function;
  }

  Range<uint> input_ids_of_node(uint node_id) const
  {
    MyNode &node = m_nodes[node_id];
    return Range<uint>(node.inputs_start,
                       node.inputs_start + node.function->signature().inputs().size());
  }

  DFGraphSocketSequence<Range<uint>> inputs_of_node(uint node_id) const
  {
    return DFGraphSocketSequence<Range<uint>>(false, this->input_ids_of_node(node_id));
  }

  Range<uint> output_ids_of_node(uint node_id) const
  {
    MyNode &node = m_nodes[node_id];
    return Range<uint>(node.outputs_start,
                       node.outputs_start + node.function->signature().outputs().size());
  }

  DFGraphSocketSequence<Range<uint>> outputs_of_node(uint node_id) const
  {
    return DFGraphSocketSequence<Range<uint>>(true, this->output_ids_of_node(node_id));
  }

  uint first_input_id_of_node(uint node_id) const
  {
    return m_nodes[node_id].inputs_start;
  }

  uint first_output_id_of_node(uint node_id) const
  {
    return m_nodes[node_id].outputs_start;
  }

  SourceInfo *source_info_of_node(uint node_id) const
  {
    return m_nodes[node_id].source_info;
  }

  const char *name_ptr_of_node(uint node_id) const
  {
    return m_nodes[node_id].function->name().c_str();
  }

  uint origin_of_input(uint input_id) const
  {
    return m_inputs[input_id].origin;
  }

  DFGraphSocket origin_of_input(DFGraphSocket input_socket) const
  {
    BLI_assert(input_socket.is_input());
    return DFGraphSocket::FromOutput(this->origin_of_input(input_socket.id()));
  }

  ArrayRef<uint> targets_of_output(uint output_id) const
  {
    OutputSocket &data = m_outputs[output_id];
    return ArrayRef<uint>(&m_targets[data.targets_start], data.targets_amount);
  }

  DFGraphSocketSequence<ArrayRef<uint>> targets_of_output(DFGraphSocket output_socket) const
  {
    BLI_assert(output_socket.is_output());
    return DFGraphSocketSequence<ArrayRef<uint>>(false,
                                                 this->targets_of_output(output_socket.id()));
  }

  uint node_id_of_socket(DFGraphSocket socket) const
  {
    if (socket.is_input()) {
      return this->node_id_of_input(socket);
    }
    else {
      return this->node_id_of_output(socket);
    }
  }

  uint node_id_of_input(uint input_id) const
  {
    return m_inputs[input_id].node;
  }

  uint node_id_of_input(DFGraphSocket input_socket) const
  {
    BLI_assert(input_socket.is_input());
    return this->node_id_of_input(input_socket.id());
  }

  uint node_id_of_output(uint output_id) const
  {
    return m_outputs[output_id].node;
  }

  uint node_id_of_output(DFGraphSocket output_socket) const
  {
    BLI_assert(output_socket.is_output());
    return this->node_id_of_output(output_socket.id());
  }

  uint index_of_socket(DFGraphSocket socket) const
  {
    if (socket.is_input()) {
      return this->index_of_input(socket);
    }
    else {
      return this->index_of_output(socket);
    }
  }

  uint index_of_input(uint input_id) const
  {
    return input_id - m_nodes[m_inputs[input_id].node].inputs_start;
  }

  uint index_of_input(DFGraphSocket input_socket) const
  {
    BLI_assert(input_socket.is_input());
    return this->index_of_input(input_socket.id());
  }

  uint index_of_output(uint output_id) const
  {
    return output_id - m_nodes[m_outputs[output_id].node].outputs_start;
  }

  uint index_of_output(DFGraphSocket output_socket) const
  {
    BLI_assert(output_socket.is_output());
    return this->index_of_output(output_socket.id());
  }

  const std::string &name_of_socket(DFGraphSocket socket)
  {
    if (socket.is_input()) {
      return this->name_of_input(socket.id());
    }
    else {
      return this->name_of_output(socket.id());
    }
  }

  SharedType &type_of_socket(DFGraphSocket socket)
  {
    if (socket.is_input()) {
      return this->type_of_input(socket.id());
    }
    else {
      return this->type_of_output(socket.id());
    }
  }

  const std::string &name_of_input(uint input_socket)
  {
    return this->input_parameter(input_socket).name();
  }

  const std::string &name_of_output(uint output_socket)
  {
    return this->output_parameter(output_socket).name();
  }

  SharedType &type_of_input(uint input_socket)
  {
    return this->input_parameter(input_socket).type();
  }

  SharedType &type_of_output(uint output_socket)
  {
    return this->output_parameter(output_socket).type();
  }

  InputParameter &input_parameter(uint input_socket)
  {
    uint node = this->node_id_of_input(input_socket);
    uint index = this->index_of_input(input_socket);
    return this->function_of_node(node)->signature().inputs()[index];
  }

  OutputParameter &output_parameter(uint output_socket)
  {
    uint node = this->node_id_of_output(output_socket);
    uint index = this->index_of_output(output_socket);
    return this->function_of_node(node)->signature().outputs()[index];
  }

  std::string to_dot();
  void to_dot__clipboard();

  void print_socket(DFGraphSocket socket) const;

 private:
  void insert_in_builder(DataFlowGraphBuilder &builder);
};

}  // namespace FN

namespace std {
template<> struct hash<FN::DFGraphSocket> {
  typedef FN::DFGraphSocket argument_type;
  typedef size_t result_type;

  result_type operator()(argument_type const &v) const noexcept
  {
    return v.id() + (int)v.is_input() * 12345;
  }
};
}  // namespace std

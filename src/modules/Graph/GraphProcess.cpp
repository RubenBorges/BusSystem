module;

#include <arpa/inet.h>
#include <cstring>
#include <cstdint>
module GraphProcess;
import std;
import WireProtocols;

using Process = GraphProcess;
using namespace protocol;



using ID = protocol::ID;
using WaveID = protocol::WaveID;
using GraphFrame = protocol::GraphFrame;

GraphProcess::GraphProcess(ID id) : identity(id), knownProcesses{id} {}

void GraphProcess::setDataHandler(DataHandler handler) {
  dataHandler_ = std::move(handler);
}

void GraphProcess::start(GraphFrame frame) {
  if (frame.kind != FrameKind::Data) {
    throw std::invalid_argument("A wave must be started with a data frame");
  }
  if (parent || activeWaves.contains(frame.waveID) || completedWaves.contains(frame.waveID)) return;
  activeWaves.insert(frame.waveID);
  std::println("Process Tree ROOT has initiated a message group.");
  go(frame);
}

void GraphProcess::go(GraphFrame frame) {
  std::vector<Channel*> childChannels;
  childChannels.reserve(children.size());
  for (ID childID : children) {
    auto it = activeChannels.find(childID);
    if (it == activeChannels.end()) {
      throw std::runtime_error("No channel registered for child GraphProcess " + std::to_string(childID));
    }
    childChannels.push_back(&it->second);
  }

  expectedMessagesByWave.insert_or_assign(frame.waveID, childChannels.size());
  if (childChannels.empty()) {
    completeWave(frame.waveID);
    return;
  }

  for (Channel* channel : childChannels) {
    std::visit([&frame](auto& implementation) { implementation.send(frame); }, *channel);
  }
}

void GraphProcess::back(GraphFrame backFrame) {
  if (backFrame.kind != FrameKind::Acknowledgement) return;

  auto outstanding = expectedMessagesByWave.find(backFrame.waveID);
  if (outstanding == expectedMessagesByWave.end() || outstanding->second == 0) return;
  if (--outstanding->second == 0) {
    completeWave(backFrame.waveID);
  }
}

void GraphProcess::completeWave(WaveID waveID) {
  expectedMessagesByWave.erase(waveID);
  activeWaves.erase(waveID);
  completedWaves.insert(waveID);

  if (parent) {
    auto it = activeChannels.find(*parent);
    if (it == activeChannels.end()) {
      throw std::runtime_error("No channel registered for parent GraphProcess " + std::to_string(*parent));
    }
GraphFrame acknowledgement{waveID, identity, {}, FrameKind::Acknowledgement};

std::visit(
        [&acknowledgement](auto& channel) { channel.send(acknowledgement); },
        it->second
    );
    return;
  }

  std::println("Process Tree ROOT has completed Wave {}. Initiating global cleanup broadcast.", waveID);
GraphFrame cleanup{waveID, identity, {}, FrameKind::Cleanup};
  for (ID childID : children) {
    auto it = activeChannels.find(childID);
    if (it == activeChannels.end()) {
      throw std::runtime_error("No channel registered for child GraphProcess " + std::to_string(childID));
    }
    std::visit([&cleanup](auto& channel) { channel.send(cleanup); }, it->second);
  }
}

void GraphProcess::pollChannels() {
  for (auto& [neighbourID, channel] : activeChannels) {
    (void)neighbourID;
    if (auto* socket = std::get_if<SocketChannel>(&channel)) {
      socket->receive(*this);
    } else if (auto* websocket = std::get_if<WebSocketChannel>(&channel)) {
      websocket->receive(*this);
    }
  }
}

bool GraphProcess::isWaveCompleted(WaveID waveID) const {
  return completedWaves.contains(waveID);
}

bool GraphProcess::isWaveCleaned(WaveID waveID) const {
  return cleanedWaves.contains(waveID);
}

//Altered neighbours parameter from value to const&
void GraphProcess::position(ID id, const std::vector<Edge>& neighbours) {
  if (neighbours.size() > std::numeric_limits<ID>::max()) {throw std::length_error("POSITION contains too many edges");}

  bool changed = false;
  if (std::find(knownProcesses.begin(), knownProcesses.end(), id) == knownProcesses.end()) {
    knownProcesses.push_back(id);
    changed = true;
  }

  for (const auto& edge : neighbours) {
    if (std::find(knownChannels.begin(), knownChannels.end(), edge) == knownChannels.end()) {
      knownChannels.push_back(edge);
      changed = true;
    }
  }

  if (!changed) return;

  Message payload;
  const auto appendID = [&payload](ID value) {
    const uint32_t networkValue = htonl(value);
    const auto* bytes = reinterpret_cast<const uint8_t*>(&networkValue);
    payload.insert(payload.end(), bytes, bytes + sizeof(networkValue));
  };
  appendID(static_cast<ID>(neighbours.size()));
  for (const auto& edge : neighbours) {
    appendID(edge.source);
    appendID(edge.destination);
  }

const GraphFrame announcement{id, identity, std::move(payload), FrameKind::Position};
  for (auto& [neighbourID, channel] : activeChannels) {
    (void)neighbourID;
    std::visit(
        [&announcement](auto& implementation) { implementation.send(announcement); },
        channel
    );
  }
}
bool GraphProcess::isProcessKnown(ID id) const {
  return std::find(knownProcesses.begin(), knownProcesses.end(), id) != knownProcesses.end();
}

void GraphProcess::receive(GraphFrame frame) {
    if (frame.kind == FrameKind::Position) {
        if (frame.payload.size() < sizeof(uint32_t)) {
            throw std::invalid_argument("Malformed POSITION frame");
        }

        const auto readID = [&frame](size_t offset) {
            uint32_t value;
            std::memcpy(&value, frame.payload.data() + offset, sizeof(value));
            return static_cast<ID>(ntohl(value));
        };
        const size_t edgeCount = readID(0);
        if (edgeCount > (std::numeric_limits<size_t>::max() - sizeof(uint32_t)) / (2 * sizeof(uint32_t)) ||
            frame.payload.size() != sizeof(uint32_t) + edgeCount * 2 * sizeof(uint32_t)) {
            throw std::invalid_argument("Malformed POSITION frame edge list");
        }

        std::vector<Edge> neighbours;
        neighbours.reserve(edgeCount);
        for (size_t i = 0; i < edgeCount; ++i) {
            const size_t offset = sizeof(uint32_t) + i * 2 * sizeof(uint32_t);
            neighbours.push_back(Edge{readID(offset), readID(offset + sizeof(uint32_t))});
        }
        position(frame.waveID, std::move(neighbours));
        return;
    }

    if (frame.kind == FrameKind::Cleanup) {
        if (!cleanedWaves.insert(frame.waveID).second) return;
        completedWaves.insert(frame.waveID);
        activeWaves.erase(frame.waveID);
        expectedMessagesByWave.erase(frame.waveID);

        for (ID childID : children) {
            auto it = activeChannels.find(childID);
            if (it == activeChannels.end()) {
                throw std::runtime_error("No channel registered for child GraphProcess " + std::to_string(childID));
            }
            std::visit([&frame](auto& channel) { channel.send(frame); }, it->second);
        }
        return;
    }

    if (completedWaves.contains(frame.waveID)) return; 

    if (frame.kind == FrameKind::Acknowledgement) {
        back(frame);
        return;
    }

    if (!activeWaves.insert(frame.waveID).second) return;
    go(frame);
    if (frame.kind == FrameKind::Data && dataHandler_) {
      dataHandler_(frame);
    }
}

void GraphProcess::registerChannel(ID neighbourID, Channel channelImplementation) {
  activeChannels.insert_or_assign(neighbourID, std::move(channelImplementation));
}

void GraphProcess::addChild(Process *child) {
  if (!child) throw std::invalid_argument("Child GraphProcess cannot be null");
  if (child->parent && child->parent != identity) {
    throw std::invalid_argument("Child GraphProcess already has a different parent");
  }
  addChild(child->identity);
  child->parent = identity;
}

void GraphProcess::addChild(ID childID) {
  if (childID == identity) throw std::invalid_argument("A GraphProcess cannot be its own child");
  if (std::find(children.begin(), children.end(), childID) == children.end()) {
    children.push_back(childID);
  }
}

void GraphProcess::setParent(ID parentID) {
  if (parentID == identity) throw std::invalid_argument("A GraphProcess cannot be its own parent");
  if (parent && parent != parentID) {
    throw std::invalid_argument("Process already has a different parent");
  }
  parent = parentID;
}

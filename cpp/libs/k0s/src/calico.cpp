#include "native.hpp"
#include <limits>

namespace skeleton::k0s {
namespace {

Json SerializePool(const KubernetesPlan& plan, const KubernetesNodePlan& node) {
    return {{"apiVersion", "crd.projectcalico.org/v1"}, {"kind", "IPPool"},
        {"metadata", {{"name", "vm-" + node.id}}},
        {"spec", {{"cidr", node.pods.ToString()}, {"blockSize", node.pods.prefix},
            {"ipipMode", "Never"}, {"vxlanMode", "Never"},
            {"natOutgoing", plan.settings.at("pod_nat_outgoing")},
            {"nodeSelector", "skeleton.vm == '" + node.id + "'"},
            {"allowedUses", Json::array({"Workload"})}}}};
}

Json SerializePeer(const KubernetesPlan& plan, const BgpPeerPlan& peer) {
    if (peer.local_asn > std::numeric_limits<std::int32_t>::max() ||
        peer.remote_asn > std::numeric_limits<std::int32_t>::max())
        throw ConfigurationError("the native Calico CRD requires ASN values to fit int32; choose a supported ASN pool");
    return {{"apiVersion", "crd.projectcalico.org/v1"}, {"kind", "BGPPeer"},
        {"metadata", {{"name", "peer-" + peer.from + "-" + peer.to}}},
        {"spec", {{"nodeSelector", "skeleton.vm == '" + peer.from + "'"},
            {"peerIP", peer.target_address + ":" + std::to_string(plan.settings.at("bgp").at("port").get<std::uint16_t>())},
            {"localASNumber", peer.local_asn},
            {"asNumber", peer.remote_asn},
            {"sourceAddress", plan.settings.at("bgp").at("sourceAddress")},
            {"nextHopMode", plan.settings.at("bgp").at("nextHopMode")}}}};
}

}  // namespace

Json SerializeCalico(const KubernetesPlan& plan) {
    Json items = Json::array();
    items.push_back({{"apiVersion", "crd.projectcalico.org/v1"}, {"kind", "BGPConfiguration"},
        {"metadata", {{"name", "default"}}},
        {"spec", {{"nodeToNodeMeshEnabled", false}, {"listenPort", plan.settings.at("bgp").at("port")}}}});
    items.push_back({{"apiVersion", "crd.projectcalico.org/v1"}, {"kind", "IPAMConfig"},
        {"metadata", {{"name", "default"}}}, {"spec", plan.settings.at("ipam")}});
    for (const auto& [id, node] : plan.nodes) items.push_back(SerializePool(plan, node));
    for (const auto& peer : plan.peers) items.push_back(SerializePeer(plan, peer));
    return {{"apiVersion", "v1"}, {"kind", "List"}, {"items", items}};
}

}  // namespace skeleton::k0s

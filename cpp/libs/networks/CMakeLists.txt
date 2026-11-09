add_library(skeleton_network_types INTERFACE)
target_include_directories(skeleton_network_types INTERFACE include)
target_link_libraries(skeleton_network_types INTERFACE skeleton_contracts)

add_library(skeleton_networks STATIC
    src/address.cpp
    src/configuration.cpp
    src/hypervisor.cpp
    src/connections.cpp
    src/dns.cpp
    src/firewall.cpp
    src/firewall_matches.cpp
    src/firewall_connections.cpp
    src/nftables.cpp
    src/kubernetes.cpp
    src/access.cpp
    src/policy.cpp
    src/ingress_guards.cpp
    src/cluster_firewall.cpp
    src/pod_access.cpp
)
target_include_directories(skeleton_networks PUBLIC include)
target_link_libraries(skeleton_networks PUBLIC skeleton_topology)

if(BUILD_TESTING)
    add_executable(skeleton_networks_tests
        tests/address_test.cpp
        tests/network_test.cpp
        tests/kubernetes_test.cpp
        tests/policy_test.cpp
    )
    target_link_libraries(skeleton_networks_tests PRIVATE skeleton_networks skeleton_test_support)
    gtest_discover_tests(skeleton_networks_tests)
endif()
target_link_libraries(skeleton_networks PRIVATE OpenSSL::Crypto)

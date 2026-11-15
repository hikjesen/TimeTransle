#include "timetransle/ipc/protocol.hpp"
#include <iostream>
int main() {
    using namespace timetransle;
    for (uint32_t operation=0; operation<=2; ++operation) {
        const ipc::CheckpointPayload input{true,true,operation,protocol::ErrorCode::checkpoint_restore_failed};
        std::array<std::byte, ipc::checkpoint_payload_bytes> bytes{};
        ipc::encode_checkpoint_payload(input, bytes);
        ipc::CheckpointPayload output;
        if(bytes.size()!=16 || !ipc::decode_checkpoint_payload(bytes,output) ||
           !output.available || !output.has_snapshot || output.operation!=operation || output.error!=input.error) return 1;
    }
    std::array<std::byte, ipc::checkpoint_payload_bytes> invalid{};
    ipc::encode_checkpoint_payload({true,false,0,protocol::ErrorCode::ok}, invalid);
    invalid[8]=std::byte{3};
    ipc::CheckpointPayload output;
    if(ipc::decode_checkpoint_payload(invalid,output))return 2;
    static_assert(ipc::checkpoint_ack_payload_bytes==56 && ipc::state_payload_bytes==40);
    std::cout<<"checkpoint protocol 1.2: passed\n";
}

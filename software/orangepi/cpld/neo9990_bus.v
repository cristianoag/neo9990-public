module neo9990_bus (
    input wire [15:0] msx_a,
    inout wire [7:0] msx_d,
    input wire msx_iorq_n, msx_rd_n, msx_wr_n, msx_m1_n,
    input wire msx_mreq_n, msx_sltsl_n, msx_reset_n, msx_clk,
    inout wire msx_wait_n, msx_int_n, msx_busdir_n,
    inout wire [15:0] host_d,
    input wire [1:0] host_sel,
    input wire host_rd_n, host_wr_n, host_ready, host_irq_n, boot_rom,
    output wire request, host_reset_out_n, host_msx_clk
);
    reg [2:0] configured_features;
    reg configured, protocol_error, armed;
    wire active_ready = host_ready & armed;
    reg [3:0] rom_pages;
    wire [2:0] features = configured ? configured_features : {1'b0, boot_rom, 1'b0};
    wire valid_strobe = msx_rd_n ^ msx_wr_n;
    wire memory_cycle = msx_reset_n && features[1] && msx_iorq_n &&
                        !msx_mreq_n && !msx_sltsl_n && valid_strobe &&
                        rom_pages[msx_a[15:14]];
    wire io_cycle = msx_reset_n && !msx_iorq_n && msx_mreq_n &&
                    msx_m1_n && valid_strobe;
    wire v9990_cycle = io_cycle && features[0] && msx_a[7:4] == 4'h6;
    wire opl4_cycle = io_cycle && features[2] &&
                     (msx_a[7:2] == 6'h31 || msx_a[7:1] == 7'h3f);
    wire selected = active_ready && (memory_cycle || v9990_cycle || opl4_cycle);
    wire reset_n = msx_reset_n & active_ready;
    reg capture_delay, pending, reply_loaded, acknowledged;
    reg [15:0] address_latch;
    reg read_latch, memory_latch;
    reg [2:0] class_latch;
    reg [7:0] write_latch, read_data, request_seq;
    reg reply_meta, reply_sync, reply_seen, reply_toggle;
    reg [7:0] reply_data;
    wire reply_outstanding = reply_toggle != reply_seen;

    // ROM boot wait is deliberately independent of host_ready.
    assign msx_wait_n = (memory_cycle && !active_ready) ||
                       (selected && !acknowledged) ? 1'b0 : 1'bz;
    assign msx_int_n = reset_n && !host_irq_n ? 1'b0 : 1'bz;
    wire driving = selected && pending && reply_loaded && read_latch && !msx_rd_n;
    assign msx_busdir_n = driving ? 1'b0 : 1'bz;
    assign msx_d = driving ? read_data : 8'bz;
    assign request = pending && !acknowledged;
    assign host_reset_out_n = msx_reset_n;
    assign host_msx_clk = msx_clk;

    // An MSX reset requires the host to reinitialize and raise READY again.
    always @(posedge host_ready or negedge msx_reset_n) begin
        if (!msx_reset_n)
            armed <= 0;
        else
            armed <= 1;
    end

    always @(posedge msx_clk or negedge reset_n) begin
        if (!reset_n) begin
            capture_delay <= 0;
            pending <= 0;
            reply_loaded <= 0;
            acknowledged <= 0;
            address_latch <= 0;
            read_latch <= 0;
            memory_latch <= 0;
            class_latch <= 0;
            write_latch <= 0;
            read_data <= 0;
            request_seq <= 0;
            reply_meta <= 0;
            reply_sync <= 0;
            reply_seen <= 0;
        end else begin
            // The host holds its reply payload until this toggle is consumed.
            reply_meta <= reply_toggle;
            reply_sync <= reply_meta;
            if (!selected) begin
                capture_delay <= 0;
                pending <= 0;
                reply_loaded <= 0;
                acknowledged <= 0;
                reply_seen <= reply_sync;
            end else if (!capture_delay) begin
                capture_delay <= 1;
            end else if (!pending) begin
                pending <= 1;
                address_latch <= msx_a;
                read_latch <= !msx_rd_n;
                memory_latch <= memory_cycle;
                class_latch <= memory_cycle ? 3'd2 : v9990_cycle ? 3'd1 : 3'd3;
                write_latch <= msx_rd_n ? msx_d : 8'h00;
                request_seq <= request_seq + 1'b1;
            end else if (!reply_loaded && reply_sync != reply_seen) begin
                read_data <= reply_data;
                reply_loaded <= 1;
                reply_seen <= reply_sync;
            end else if (reply_loaded) begin
                // Drive read data for a complete slot clock before releasing WAIT.
                acknowledged <= 1;
            end
        end
    end

    reg [15:0] host_read_data;
    always @* begin
        case (host_sel)
            2'd0: host_read_data = {request, read_latch, memory_latch,
                                   msx_reset_n, protocol_error, class_latch, write_latch};
            2'd1: host_read_data = address_latch;
            2'd2: host_read_data = {request_seq, 8'hd4};
            2'd3: host_read_data = {boot_rom, armed, 2'b00, rom_pages, 5'b00000, features};
        endcase
    end
    wire host_drive_enable = msx_reset_n && !host_rd_n && host_wr_n;
    assign host_d = host_drive_enable ? host_read_data : 16'bz;

    // Configuration survives READY changes, but is reset by the MSX reset input.
    always @(posedge host_wr_n or negedge msx_reset_n) begin
        if (!msx_reset_n) begin
            configured_features <= 0;
            configured <= 0;
            rom_pages <= 4'b0110;
            protocol_error <= 0;
        end else if (!host_rd_n) begin
            protocol_error <= 1;
        end else begin
            case (host_sel)
                2'd0: if (host_d[0]) protocol_error <= 0;
                2'd1: protocol_error <= 1;
                2'd2: if (!active_ready || !request || reply_outstanding ||
                          reply_loaded || host_d[15:8] != request_seq)
                          protocol_error <= 1;
                2'd3: if (host_ready || (host_d & 16'hf0f8) != 0) begin
                          protocol_error <= 1;
                      end else begin
                          configured_features <= host_d[2:0];
                          rom_pages <= host_d[11:8];
                          configured <= 1;
                      end
            endcase
        end
    end

    always @(posedge host_wr_n or negedge reset_n) begin
        if (!reset_n) begin
            reply_data <= 0;
            reply_toggle <= 0;
        end else if (host_rd_n && host_sel == 2'd2 && request &&
                     !reply_outstanding && !reply_loaded && host_d[15:8] == request_seq) begin
            reply_data <= host_d[7:0];
            reply_toggle <= ~reply_toggle;
        end
    end
endmodule

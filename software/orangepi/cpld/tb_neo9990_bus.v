`timescale 1ns/1ps
module tb_neo9990_bus;
    reg [15:0] msx_a = 0;
    reg [7:0] write_value = 0;
    reg write_drive = 0;
    tri [7:0] msx_d;
    tri1 msx_wait_n, msx_int_n, msx_busdir_n;
    reg msx_iorq_n = 1, msx_rd_n = 1, msx_wr_n = 1, msx_m1_n = 1;
    reg msx_mreq_n = 1, msx_sltsl_n = 1, msx_reset_n = 0, msx_clk = 0;
    reg [15:0] host_value = 0;
    reg host_drive = 0;
    tri0 [15:0] host_d;
    reg [1:0] host_sel = 0;
    reg host_rd_n = 1, host_wr_n = 1, host_ready = 0, host_irq_n = 1, boot_rom = 1;
    wire request, host_reset_out_n, host_msx_clk;
    integer checks = 0, address, kind;
    reg [7:0] rom [0:65535];
    reg [15:0] status, captured_address, ticket, config_word;
    reg [7:0] expected;
    assign msx_d = write_drive ? write_value : 8'bz;
    assign host_d = host_drive ? host_value : 16'bz;
    always #139.7 msx_clk = ~msx_clk;
    neo9990_bus dut (
        .msx_a(msx_a), .msx_d(msx_d), .msx_iorq_n(msx_iorq_n),
        .msx_rd_n(msx_rd_n), .msx_wr_n(msx_wr_n), .msx_m1_n(msx_m1_n),
        .msx_mreq_n(msx_mreq_n), .msx_sltsl_n(msx_sltsl_n),
        .msx_reset_n(msx_reset_n), .msx_clk(msx_clk), .msx_wait_n(msx_wait_n),
        .msx_int_n(msx_int_n), .msx_busdir_n(msx_busdir_n),
        .host_d(host_d), .host_sel(host_sel), .host_rd_n(host_rd_n),
        .host_wr_n(host_wr_n), .host_ready(host_ready), .host_irq_n(host_irq_n),
        .boot_rom(boot_rom), .request(request),
        .host_reset_out_n(host_reset_out_n), .host_msx_clk(host_msx_clk)
    );

    task check;
        input condition;
        input [511:0] message;
        begin
            checks = checks+1;
            if (condition !== 1'b1) begin
                $display("FAIL at %0t address=%04x: %0s", $time, msx_a, message);
                $fatal(1);
            end
        end
    endtask
    task clocks;
        input integer count;
        begin repeat (count) @(negedge msx_clk); #10; end
    endtask
    task host_read;
        input [1:0] regno;
        output [15:0] value;
        begin
            check(!host_drive && host_wr_n, "host changes to input before RD");
            host_sel = regno;
            #80;
            host_rd_n = 0;
            #100;
            value = host_d;
            check((^value) !== 1'bx, "parallel host read has valid data");
            host_rd_n = 1;
            #80;
            check(!dut.host_drive_enable && host_d === 0, "CPLD releases biased host data after RD");
        end
    endtask
    task host_write;
        input [1:0] regno;
        input [15:0] value;
        begin
            check(host_rd_n, "host disables RD before driving data");
            host_sel = regno;
            host_value = value;
            host_drive = 1;
            #80;
            host_wr_n = 0;
            #100;
            check(host_d === value, "CPLD does not contend on host write");
            host_wr_n = 1;
            #80;
            host_drive = 0;
        end
    endtask
    task begin_cycle;
        input [15:0] addr;
        input is_read;
        input is_memory;
        begin
            @(negedge msx_clk);
            msx_a = addr;
            msx_iorq_n = is_memory;
            msx_mreq_n = !is_memory;
            msx_sltsl_n = !is_memory;
            msx_rd_n = !is_read;
            msx_wr_n = is_read;
            write_drive = !is_read;
            #1;
        end
    endtask
    task end_cycle;
        begin
            msx_iorq_n = 1;
            msx_mreq_n = 1;
            msx_sltsl_n = 1;
            msx_rd_n = 1;
            msx_wr_n = 1;
            write_drive = 0;
            clocks(3);
            check(!request && msx_wait_n && msx_busdir_n, "cycle end releases control");
            check(msx_d === 8'bz, "cycle end releases MSX data");
        end
    endtask
    task fetch_request;
        input [2:0] expected_class;
        begin
            clocks(3);
            check(request && !msx_wait_n && msx_busdir_n, "request waits without data drive");
            host_read(0, status);
            host_read(1, captured_address);
            host_read(2, ticket);
            check(status[15] && status[14] == !msx_rd_n, "pending and operation captured");
            check(status[13] == !msx_mreq_n && status[10:8] == expected_class,
                  "memory flag and device class captured");
            check(captured_address == msx_a, "all sixteen address bits captured");
            check(ticket[7:0] == 8'hd4, "parallel protocol signature");
            if (!msx_wr_n)
                check(status[7:0] == write_value, "write byte captured");
        end
    endtask
    task reply;
        input [7:0] value;
        real ready_time;
        begin
            host_write(2, {ticket[15:8], value});
            wait(dut.reply_loaded);
            #0.1;
            ready_time=$realtime;
            check(!msx_wait_n && !dut.acknowledged, "reply data precedes WAIT release");
            if (!msx_rd_n)
                check(msx_d === value, "read data valid while WAIT remains asserted");
            @(posedge msx_wait_n);
            #0.1;
            check($realtime-ready_time >= 279.0, "data setup spans a full slot clock");
            clocks(2);
            check(!request && msx_wait_n, "reply releases WAIT");
            if (!msx_rd_n) begin
                check(!msx_busdir_n && msx_d === value, "read reply on MSX data");
            end else begin
                check(msx_busdir_n && msx_d === write_value, "write never drives MSX data");
            end
            clocks(2);
            check(!request, "held strobe does not duplicate request");
        end
    endtask

    initial begin
        for (address=0; address<65536; address=address+1)
            rom[address] = address ^ (address >> 8);
        rom[16'h4000] = 8'h41;
        rom[16'h4001] = 8'h42;
        #1; host_ready=1; msx_reset_n=1;
        #1; host_ready=0; msx_reset_n=0;
        clocks(2);
        msx_reset_n=1;
        clocks(2);
        host_read(3, config_word);
        check(config_word == 16'h8602, "jumper selects default boot ROM pages 1/2");
        begin_cycle(16'h4000, 1, 1);
        clocks(4);
        check(!msx_wait_n && !request && msx_d === 8'bz, "ROM waits for Pi before READY");
        host_write(3, 16'h0607);
        host_ready=1;
        fetch_request(2);
        reply(rom[captured_address]);
        check(msx_d === 8'h41, "first BIOS byte can be served from host RAM");
        end_cycle;
        for (address=0; address<256; address=address+1) begin
            begin_cycle(16'hab00 | address, 1, 0);
            kind = ((address & 240) == 96) ? 1 :
                   ((address >= 196 && address <= 199) || address == 126 || address == 127) ? 3 : 0;
            if (kind != 0) begin
                fetch_request(kind);
                reply(address ^ 8'ha5);
            end else begin
                clocks(3);
                check(!request && msx_wait_n && msx_d === 8'bz, "unselected IO ignored");
            end
            end_cycle;
        end
        // Sweep the complete address space, including opcode fetches with M1 low.
        msx_m1_n=0;
        for (address=0; address<65536; address=address+1) begin
            begin_cycle(address, 1, 1);
            if (address >= 16384 && address < 49152) begin
                fetch_request(2);
                expected=rom[captured_address];
                reply(expected);
                check(msx_d === rom[address], "host RAM ROM byte matches full address");
            end else begin
                clocks(3);
                check(!request && msx_wait_n && msx_d === 8'bz, "disabled ROM page ignored");
            end
            end_cycle;
        end
        msx_m1_n=1;
        for (address=0; address<32; address=address+1) begin
            write_value=address ^ 8'h5a;
            begin_cycle(16'h6000+address, 0, 1);
            fetch_request(2);
            reply(0);
            end_cycle;
        end
        for (address=196; address<=199; address=address+1) begin
            write_value=address;
            begin_cycle(address, 0, 0);
            fetch_request(3);
            reply(0);
            end_cycle;
        end
        begin_cycle(16'h7e, 0, 0);
        write_value=8'h23;
        fetch_request(3);
        reply(0);
        end_cycle;
        begin_cycle(16'h7f, 0, 0);
        write_value=8'h67;
        fetch_request(3);
        reply(0);
        end_cycle;

        begin_cycle(16'h6060, 1, 1);
        fetch_request(2);
        host_write(2, {ticket[15:8] ^ 8'h80, 8'h11});
        clocks(5);
        check(request && !msx_wait_n && msx_d === 8'bz, "wrong token cannot ACK");
        host_read(0, status);
        check(status[11], "bad commit sets observable error");
        host_write(0, 1);
        host_read(0, status);
        check(!status[11], "host explicitly clears protocol error");
        host_sel=2;
        host_rd_n=0;
        #40;
        host_wr_n=0;
        #40;
        check(!dut.host_drive_enable && host_d === 0, "overlapping host strobes tristate data");
        host_wr_n=1;
        #40;
        host_rd_n=1;
        host_read(0, status);
        check(status[11] && request && !msx_wait_n, "overlapping host strobes cannot ACK");
        host_write(0, 1);
        host_write(3, 0);
        host_read(3, config_word);
        check(config_word[11:8] == 6 && config_word[2:0] == 7, "live reconfiguration rejected");
        reply(8'h82);
        host_write(2, {ticket[15:8], 8'h99});
        clocks(3);
        check(msx_d === 8'h82, "duplicate commit cannot change read data");
        end_cycle;
        begin_cycle(16'h60, 1, 0);
        msx_wr_n=0;
        clocks(3);
        check(msx_wait_n && !request && msx_d === 8'bz, "simultaneous MSX RD/WR ignored");
        end_cycle;

        msx_m1_n=0;
        begin_cycle(16'h60, 1, 0);
        clocks(3);
        check(msx_wait_n && !request, "interrupt acknowledge not decoded as IO");
        end_cycle;
        msx_m1_n=1;
        begin_cycle(16'h4000, 1, 1);
        msx_sltsl_n=1;
        clocks(3);
        check(msx_wait_n && !request, "other cartridge slot ignored");
        end_cycle;
        begin_cycle(16'h4000, 1, 1);
        msx_rd_n=1;
        clocks(3);
        check(msx_wait_n && !request, "refresh/no-strobe memory cycle ignored");
        end_cycle;
        host_irq_n=0;
        #10;
        check(!msx_int_n, "host IRQ asserts open-drain");
        host_ready=0;
        #10;
        check(msx_int_n && msx_busdir_n, "READY low releases IRQ and data direction");
        host_irq_n=1;
        host_write(3, 16'h0f02);
        host_ready=1;
        begin_cycle(16'hffff, 1, 1);
        fetch_request(2);
        reply(8'hc7);
        end_cycle;

        host_ready=0;
        boot_rom=0;
        msx_reset_n=0;
        clocks(2);
        msx_reset_n=1;
        begin_cycle(16'h4000, 1, 1);
        clocks(4);
        check(msx_wait_n && !request, "no boot jumper means no startup stall");
        end_cycle;
        host_ready=1;
        boot_rom=1;
        msx_reset_n=0;
        clocks(2);
        msx_reset_n=1;
        begin_cycle(16'h4000, 1, 1);
        clocks(4);
        check(!msx_wait_n && !request, "reset requires READY rearm even when host pin stays high");
        host_read(3, config_word);
        check(!config_word[14], "configuration exposes disarmed reset state");
        end_cycle;
        host_ready=0;
        boot_rom=0;
        host_write(3, 16'h0601);
        host_ready=1;
        begin_cycle(16'h7e, 1, 0);
        clocks(3);
        check(msx_wait_n && !request, "OPL4 profile can be disabled");
        end_cycle;
        begin_cycle(16'h60, 1, 0);
        fetch_request(1);
        msx_reset_n=0;
        #10;
        check(!request && msx_wait_n && msx_d === 8'bz && host_reset_out_n === 0,
              "reset cancels request and forwards reset");
        end_cycle;
        host_ready=0;
        msx_reset_n=1;
        clocks(3);
        host_write(3, 16'h0600);
        host_ready=1;
        begin_cycle(16'h60, 1, 0);
        clocks(3);
        check(!request && msx_wait_n, "all profiles disabled means no IO interception");
        end_cycle;
        check(host_msx_clk === msx_clk, "forwarded slot clock");
        $display("PASS: %0d parallel ROM/V9990/OPL4 interface checks", checks);
        $finish;
    end
    initial begin #2000000000; $fatal(1, "simulation timeout"); end
endmodule

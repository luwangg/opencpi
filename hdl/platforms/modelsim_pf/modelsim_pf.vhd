-- THIS FILE WAS ORIGINALLY GENERATED ON Thu Sep 26 15:32:12 2013 EDT
-- BASED ON THE FILE: modelsim_pf.xml
-- YOU *ARE* EXPECTED TO EDIT IT
-- This file initially contains the architecture skeleton for worker: modelsim_pf

library IEEE, ocpi, platform, sdp;
use IEEE.std_logic_1164.all, ieee.numeric_std.all, ocpi.types.all, platform.platform_pkg.all,
  sdp.all;

architecture rtl of modelsim_pf_worker is
  signal   ctl_clk          : std_logic;
  signal   ctl_reset        : std_logic;
  signal   ctl_rst_n        : std_logic;
begin
  ctl_rst_n <= not ctl_reset; -- for those that need it

  -- generate a clock
  clock : sim_clk
    port map(clk => ctl_clk, reset => ctl_reset);

  -- This piece of generic infrastructure in is instantiated here because
  -- it localizes all these signals here in the platform worker, and thus
  -- the platform worker simply produces clock, reset, and time, all in the
  -- clock domain of the timekeepping clock.
  ts : time_server
    port map(
      CLK                     => ctl_clk,
      RST_N                   => ctl_rst_n,
      timeCLK                 => ctl_clk,
      timeRST_N               => ctl_rst_n,

      timeControl             => props_in.timeControl,
      timeControl_written     => props_in.timeControl_written,
      timeStatus              => props_out.timeStatus,
      timeNowIn               => props_in.timeNow,
      timeNow_written         => props_in.timeNow_written,
      timeNowOut              => props_out.timeNow,
      timeDeltaIn             => props_in.timeDelta,
      timeDelta_written       => props_in.timeDelta_written,
      timeDeltaOut            => props_out.timeDelta,
      ticksPerSecond          => props_out.ticksPerSecond,
      
      -- PPS interface
      ppsIn                   => '0', -- we could actually generate this
      ppsOut                  => open,

      -- Time service output
      time_service            => time_out
      );

  sdp_sim_i : sdp.sdp.sdp_sim
    generic map(ocpi_debug => ocpi_debug,
                sdp_width  => sdp_width)
    port map(clk => ctl_clk,
             reset => ctl_reset,
             sdp_in => sdp_in,
             sdp_out => sdp_out,
             sdp_in_data => sdp_in_data,
             sdp_out_data => sdp_out_data);
  sdp_term_i : sdp.sdp.sdp_term
    generic map(ocpi_debug => ocpi_debug,
                sdp_width => sdp_width)
    port map(up_in => sdp_slave_in,
             up_in_data => sdp_slave_in_data,
             up_out => sdp_slave_out,
             up_out_data => sdp_slave_out_data,
             drop_count => props_out.sdpDropCount);

  props_out.platform          <= to_string("modelsim_pf", props_out.platform'length-1);
  props_out.dna               <= (others => '0');
  props_out.nSwitches         <= (others => '0');
  props_out.switches          <= (others => '0');
  props_out.memories_length   <= to_ulong(1);
  props_out.memories          <= (others => to_ulong(0));
  props_out.nLEDs             <= (others => '0');
  props_out.UUID              <= metadata_in.UUID;
  props_out.romData           <= metadata_in.romData;
  metadata_out.clk            <= ctl_clk;
  metadata_out.romAddr        <= props_in.romAddr;
  metadata_out.romEn          <= props_in.romData_read;
end rtl;

library ieee; use ieee.std_logic_1164.all, ieee.numeric_std.all;
library ocpi; use ocpi.types.all;
entity plusarg is
   generic(length     : natural := work.util.plusarg_length;
           name       : string);
   port   (val        : out ocpi.types.string_t(0 to length));
end entity plusarg;
architecture rtl of plusarg is
  signal val_internal : std_logic_vector(0 to length*8-1);
  function slv2string(input : std_logic_vector) return string_t is
    variable result: string_t(0 to length); -- room for null
    variable n: natural := 0;
    variable i: natural := 0;
  begin
    -- skip null chars at the "left" side of the plusarg slv
    while n < length loop
      if unsigned(input(n*8 to n*8+7)) /= 0 then
        exit;
      end if;
      n := n + 1;
    end loop;
    -- copy remaining chars that are right justified to result
    while n < length loop
      result(i) := signed(input(n*8 to n*8+7));
      n := n + 1;
      i := i + 1;
    end loop;
    -- fill the rest of the result with null chars
    while i <= length loop          -- <= because result has extra room for the null
      result(i) := (others => '0');
      i := i + 1;
    end loop;
    return result;
  end slv2string;
begin
  val      <= slv2string(val_internal); -- convert from right-justified plusarg format
  plusarg_i: component work.util.plusarg_internal -- instance the verilog that grabs the plusarg
    generic map(length => length, name => name)
    port    map(val    => val_internal);
end rtl;

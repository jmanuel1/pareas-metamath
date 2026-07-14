# ignored: whitespace, comment

metamath -> database;
database [database_empty] -> ;
database [database_statement] -> statement database;
database [database_include] -> file_inclusion_command database;
database [database_constant_decl] -> constant_declaration database;

file_inclusion_command -> 'dollar_lbracket' 'math_symbol' 'dollar_rbracket';

statement [statement_block] -> block;
statement [statement_variable_decl] -> variable_declaration;
statement [statement_f] -> f_statement;
statement [statement_e] -> e_statement;
statement [statement_d] -> d_statement;

block -> 'dollar_lbrace' statement_many 'dollar_rbrace';

variable_declaration -> 'dollar_v' math_symbol_some 'dollar_period';
# must be in outermost block
constant_declaration -> 'dollar_c' math_symbol_some 'dollar_period';

f_statement -> 'label' dollar_f 'math_symbol' 'math_symbol' 'dollar_period';
e_statement -> 'label' dollar_e 'math_symbol' math_symbol_many 'dollar_period';

d_statement -> 'dollar_d' 'math_symbol' math_symbol_some 'dollar_period';

a_statement -> 'label' dollar_a 'math_symbol' math_symbol_many 'dollar_period';
p_statement -> 'label' dollar_p 'math_symbol' math_symbol_many 'dollar_equal' label_many 'dollar_period';

label_many [label_empty] -> ;
label_many [label_some] -> 'label' label_many ;

math_symbol_many [math_symbol_empty] -> ;
math_symbol_many [math_symbol_some] -> math_symbol_some ;
math_symbol_some [math_symbol_one] -> 'math_symbol';
math_symbol_some [math_symbol_multiple] -> 'math_symbol' math_symbol_some;

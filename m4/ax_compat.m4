# For compatibility with autoconf < 2.63b
m4_ifndef([AS_VAR_COPY],
  [AC_DEFUN([AS_VAR_COPY],
     [AS_LITERAL_IF([$1[]$2], [$1=$$2], [eval $1=\$$2])])])

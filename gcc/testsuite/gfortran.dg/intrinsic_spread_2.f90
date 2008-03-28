! { dg-do run }
! { dg-require-effective-target fortran_large_real }
program foo
   implicit none
   integer,parameter :: k = selected_real_kind (precision (0.0_8) + 1)

   real(kind=k), dimension(10) :: r_k
   real(kind=k), dimension (2, 3) :: ar_k
   real(kind=k), dimension (2, 2, 3) :: br_k
   character (len=200) line1, line2, line3

   ar_k = reshape ((/1._k, 2._k, 3._k, 4._k, 5._k, 6._k/), (/2, 3/))
   br_k = spread (ar_k, 1, 2)
   if (any (br_k .ne. reshape ((/1._k, 1._k, 2._k, 2._k, 3._k, 3._k, &
   & 4._k, 4._k, 5._k, 5._k, 6._k, 6._k/), (/2, 2, 3/)))) call abort
   line1 = ' '
   write(line1, 9010) br_k
   line2 = ' '
   write(line2, 9010) spread (ar_k, 1, 2)
   if (line1 /= line2) call abort
   line3 = ' '
   write(line3, 9010) spread (ar_k, 1, 2) + 0._k
   if (line1 /= line3) call abort
   r_k = spread(1._k,1,10)
   if (any(r_k /= 1._k)) call abort

9010 format(12F7.3)

end program

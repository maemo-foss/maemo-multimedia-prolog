%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% This file is part of libtrace
% 
% Copyright (C) 2010 Nokia Corporation.
% 
% This library is free software; you can redistribute
% it and/or modify it under the terms of the GNU Lesser General Public
% License as published by the Free Software Foundation
% version 2.1 of the License.
% 
% This library is distributed in the hope that it will be useful,
% but WITHOUT ANY WARRANTY; without even the implied warranty of
% MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
% Lesser General Public License for more details.
% 
% You should have received a copy of the GNU Lesser General Public
% License along with this library; if not, write to the Free Software
% Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
% USA.
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%


:- module(predicates, [success/1, failure/1, exception/1, echo/2]).

rules([success/1, failure/1, exception/1, echo/2, undefined/1]).

% always succeed
success([[success, [always, succeeds]]]).

% always fail
failure(_) :- fail.

% raise an exception
exception(A) :- A is 1 / 0.

% 'echo' any given argument back
echo(A, List) :-
        writef('echo got %w', [A]),
	List = [[echoed, [value, A]]].


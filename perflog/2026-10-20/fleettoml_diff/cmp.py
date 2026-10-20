import sys
c=open('corpus.txt',encoding='utf-8',errors='replace').read().split('\x01\x01\x01\n')
A=open('out_a_tomlpp.txt',encoding='utf-8',errors='replace').read().split('\n'); B=open('out_b_mini.txt',encoding='utf-8',errors='replace').read().split('\n'); A3=open('out_a_tomlpp33.txt',encoding='utf-8',errors='replace').read().split('\n')
n=min(len(c),len(A),len(B))
print('cases',len(c),'outputs',len(A),len(B))
diff_acc=[];diff_rej=[];diff_val=[];d34=0
for i in range(n):
    if A[i]!=A3[i]: d34+=1
    if A[i]==B[i]: continue
    if A[i]=='ERROR': diff_rej.append(i)
    elif B[i]=='ERROR': diff_acc.append(i)
    else: diff_val.append(i)
print('3.4 vs 3.3 differ',d34)
print('we reject what toml++ accepts:',len(diff_acc),' we accept what toml++ rejects:',len(diff_rej),' different values:',len(diff_val))
def show(lst,tag,k=15):
    print('---',tag)
    for i in lst[:k]: print(repr(c[i][:110]),'| toml++:',A[i][:70],'| mini:',B[i][:70])
show(diff_acc,'REJECTED BY US')
show(diff_val,'VALUE DIFF')
show(diff_rej,'ACCEPTED BY US',25)
